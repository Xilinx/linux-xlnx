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
*       (c) Copyright 2005-2008 Xilinx Inc.
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
 * @file xlltemac_control.c
 *
 * Functions in this file implement general purpose command and control related
 * functionality. See xlltemac.h for a detailed description of the driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jvb  11/10/06 First release
 * </pre>
 *****************************************************************************/

/***************************** Include Files *********************************/

#include "xlltemac.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
 * in the TEMAC channel's multicast filter list.
 *
 * XLlTemac_MulticastAdd adds the Ethernet address, <i>AddressPtr</i> to the
 * TEMAC channel's multicast filter list, at list index <i>Entry</i>. The
 * address referenced by <i>AddressPtr</i> may be of any unicast, multicast, or
 * broadcast address form. The harware for the TEMAC channel can hold up to
 * XTE_MULTI_MAT_ENTRIES addresses in this filter list.<br><br>
 *
 * The device must be stopped to use this function.<br><br>
 *
 * Once an Ethernet address is programmed, the TEMAC channel will begin
 * receiving data sent from that address. The TEMAC hardware does not have a
 * control bit to disable multicast filtering. The only way to prevent the
 * TEMAC channel from receiving messages from an Ethernet address in the
 * Multicast Address Table (MAT) is to clear it with XLlTemac_MulticastClear().
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param AddressPtr is a pointer to the 6-byte Ethernet address to set. The
 *        previous address at the location <i>Entry</i> (if any) is overwritten
 *        with the value at <i>AddressPtr</i>.
 * @param Entry is the hardware storage location to program this address and
 *        must be between 0..XTE_MULTI_MAT_ENTRIES-1. 
 *
 * @return On successful completion, XLlTemac_MulticastAdd returns XST_SUCCESS.
 *         Otherwise, if the TEMAC channel is not stopped, XLlTemac_MulticastAdd
 *         returns XST_DEVICE_IS_STARTED.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_MulticastAdd(XLlTemac *InstancePtr, void *AddressPtr, int Entry)
{
	u32 Maw0Reg;
	u32 Maw1Reg;
	u8 *Aptr = (u8 *) AddressPtr;
	u32 Rdy;
	int MaxWait = 100;
	u32 BaseAddress = InstancePtr->Config.BaseAddress;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(AddressPtr != NULL);
	XASSERT_NONVOID(Entry < XTE_MULTI_MAT_ENTRIES);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastAdd\n");

	/* The device must be stopped before clearing the multicast hash table */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_MulticastAdd: returning DEVICE_IS_STARTED\n");

		return (XST_DEVICE_IS_STARTED);
	}

#ifndef CONFIG_XILINX_LL_TEMAC_EXT
	/* Set MAC bits [31:0] */
	Maw0Reg = Aptr[0];
	Maw0Reg |= Aptr[1] << 8;
	Maw0Reg |= Aptr[2] << 16;
	Maw0Reg |= Aptr[3] << 24;
	
	/* Set MAC bits [47:32] */
	Maw1Reg = Aptr[4];
	Maw1Reg |= Aptr[5] << 8;
	
	/* Add in MAT address */
	Maw1Reg |= (Entry << XTE_MAW1_MATADDR_SHIFT_MASK);
	
	/* Program HW */
	xdbg_printf(XDBG_DEBUG_GENERAL, "Setting MAT entry: %d\n", Entry);
	XLlTemac_WriteReg(BaseAddress, XTE_LSW_OFFSET, Maw0Reg);
	XLlTemac_WriteReg(BaseAddress, XTE_CTL_OFFSET,
			  XTE_MAW0_OFFSET | XTE_CTL_WEN_MASK);
	Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
	while (MaxWait && (!(Rdy & XTE_RDY_HARD_ACS_RDY_MASK))) {
	  Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
	  xdbg_stmnt(if (MaxWait == 100) {
	      xdbg_printf(XDBG_DEBUG_GENERAL,
			  "RDY reg not initially ready\n"); });
	  MaxWait--;
	  xdbg_stmnt(if (MaxWait == 0) {
	      xdbg_printf (XDBG_DEBUG_GENERAL,
			   "RDY reg never showed ready\n"); });
	}
	
	XLlTemac_WriteReg(BaseAddress, XTE_LSW_OFFSET,
			  Maw1Reg);
	XLlTemac_WriteReg(BaseAddress, XTE_CTL_OFFSET,
			  XTE_MAW1_OFFSET | XTE_CTL_WEN_MASK);
	Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
	while (MaxWait && (!(Rdy & XTE_RDY_HARD_ACS_RDY_MASK))) {
	  Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
	  xdbg_stmnt(if (MaxWait == 100) {
	      xdbg_printf(XDBG_DEBUG_GENERAL,
			  "RDY reg not initially ready\n"); });
	  MaxWait--;
	  xdbg_stmnt(if (MaxWait == 0) {
	      xdbg_printf (XDBG_DEBUG_GENERAL,
			   "RDY reg never showed ready\n"); });
	}
#else
	/* Verify if address is a good/valid multicast address, between
         * 01:00:5E:00:00:00 to 01:00:5E:7F:FF:FF per RFC1112.
	 * This address is referenced to be index to BRAM table.
	 */
	if ((0x01 == Aptr[0]) && (0x00 == Aptr[1]) &&
	    (0x5e == Aptr[2]) && (0x0 == (Aptr[3] & 0x80))){
	  /* Ext mode */
	  u32 index = ((Aptr[3] << 8) | Aptr[4]) << 2;
	  XLlTemac_WriteReg(BaseAddress, XTE_MCAST_BRAM_OFFSET + index, 0x1);
	  /* Debug */
	  xdbg_printf(XDBG_DEBUG_GENERAL, "MulticastAdd: index %d / 0x%x " \
		      "(%02x:%02x:%02x:%02x:%02x:%02x) enabled\n", index >> 2,
		      index, Aptr[0], Aptr[1], Aptr[2], Aptr[3], Aptr[4], Aptr[5]);
	  
	}else{
	  xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastAdd: returning INVALID\n");
	  return (XST_INVALID_PARAM);
	}
#endif
	
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastAdd: returning SUCCESS\n");
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * XLlTemac_MulticastGet gets the Ethernet address stored at index <i>Entry</i>
 * in the TEMAC channel's multicast filter list.<br><br>
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param AddressPtr references the memory buffer to store the retrieved
 *        Ethernet address. This memory buffer must be at least 6 bytes in
 *        length.
 * @param Entry is the hardware storage location from which to retrieve the
 *        address and must be between 0..XTE_MULTI_MAT_ENTRIES-1. 
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
void XLlTemac_MulticastGet(XLlTemac *InstancePtr, void *AddressPtr, int Entry)
{
	u32 Maw0Reg;
	u32 Maw1Reg;
	u8 *Aptr = (u8 *) AddressPtr;
	u32 Rdy;
	int MaxWait = 100;
	u32 BaseAddress = InstancePtr->Config.BaseAddress;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(Entry < XTE_MULTI_MAT_ENTRIES);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET) &
			XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastGet\n");

	/*
	 * Tell HW to provide address stored in given entry.
	 * In this case, the Access is a little weird, becuase we need to
	 * write the LSW register first, then initiate a write operation,
	 * even though it's a read operation.
	 */
	xdbg_printf(XDBG_DEBUG_GENERAL, "Getting MAT entry: %d\n", Entry);
	XLlTemac_WriteReg(BaseAddress, XTE_LSW_OFFSET,
			 Entry << XTE_MAW1_MATADDR_SHIFT_MASK | XTE_MAW1_RNW_MASK);
	XLlTemac_WriteReg(BaseAddress, XTE_CTL_OFFSET,
			 XTE_MAW1_OFFSET | XTE_CTL_WEN_MASK);
	Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
	while (MaxWait && (!(Rdy & XTE_RDY_HARD_ACS_RDY_MASK))) {
		Rdy = XLlTemac_ReadReg(BaseAddress, XTE_RDY_OFFSET);
		xdbg_stmnt(
			if (MaxWait == 100) {
				xdbg_printf(XDBG_DEBUG_GENERAL,
					    "RDY reg not initially ready\n");
			}
		);
		MaxWait--;
		xdbg_stmnt(
			if (MaxWait == 0) {
				xdbg_printf(XDBG_DEBUG_GENERAL,
					     "RDY reg never showed ready\n");
			}
		)

	}
	Maw0Reg = XLlTemac_ReadReg(BaseAddress, XTE_LSW_OFFSET);
	Maw1Reg = XLlTemac_ReadReg(BaseAddress, XTE_MSW_OFFSET);
	
	/* Copy the address to the user buffer */
	Aptr[0] = (u8) Maw0Reg;
	Aptr[1] = (u8) (Maw0Reg >> 8);
	Aptr[2] = (u8) (Maw0Reg >> 16);
	Aptr[3] = (u8) (Maw0Reg >> 24);
	Aptr[4] = (u8) Maw1Reg;
	Aptr[5] = (u8) (Maw1Reg >> 8);
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastGet: done\n");
}

/*****************************************************************************/
/**
 * XLlTemac_MulticastClear clears the Ethernet address stored at index <i>Entry</i>
 * in the TEMAC channel's multicast filter list.<br><br>
 *
 * The device must be stopped to use this function.<br><br>
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param Entry is the HW storage location used when this address was added.
 *        It must be between 0..XTE_MULTI_MAT_ENTRIES-1.
 * @param Entry is the hardware storage location to clear and must be between
 *        0..XTE_MULTI_MAT_ENTRIES-1. 
 *
 * @return On successful completion, XLlTemac_MulticastClear returns XST_SUCCESS.
 *         Otherwise, if the TEMAC channel is not stopped, XLlTemac_MulticastClear
 *         returns XST_DEVICE_IS_STARTED.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_MulticastClear(XLlTemac *InstancePtr, int Entry)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(Entry < XTE_MULTI_MAT_ENTRIES);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);
	
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_MulticastClear\n");

	/* The device must be stopped before clearing the multicast hash table */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_MulticastClear: returning DEVICE_IS_STARTED\n");
		return (XST_DEVICE_IS_STARTED);
	}

#ifndef CONFIG_XILINX_LL_TEMAC_EXT
	/* Clear the entry by writing 0:0:0:0:0:0 to it */
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_MAW0_OFFSET, 0);
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_MAW1_OFFSET, Entry << XTE_MAW1_MATADDR_SHIFT_MASK);
#else
	/* Ext mode */
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
			  XTE_MCAST_BRAM_OFFSET +
			  ((0x7fff & Entry) << 2), 0x0);
#endif
	
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_MulticastClear: returning SUCCESS\n");
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * XLlTemac_SetMacPauseAddress sets the MAC address used for pause frames to
 * <i>AddressPtr</i>. <i>AddressPtr</i> will be the address the TEMAC channel
 * will recognize as being for pause frames. Pause frames transmitted with
 * XLlTemac_SendPausePacket() will also use this address.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param AddressPtr is a pointer to the 6-byte Ethernet address to set.
 *
 * @return On successful completion, XLlTemac_SetMacPauseAddress returns
 *         XST_SUCCESS. Otherwise, if the TEMAC channel is not stopped,
 *         XLlTemac_SetMacPauseAddress returns XST_DEVICE_IS_STARTED.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_SetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);
	
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetMacPauseAddress\n");
	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_SetMacPauseAddress: returning DEVICE_IS_STARTED\n");
		return (XST_DEVICE_IS_STARTED);
	}

	/* Set the MAC bits [31:0] in ERXC0 */
	MacAddr = Aptr[0];
	MacAddr |= Aptr[1] << 8;
	MacAddr |= Aptr[2] << 16;
	MacAddr |= Aptr[3] << 24;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_RCW0_OFFSET, MacAddr);

	/* ERCW1 contains other info that must be preserved */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_RCW1_OFFSET);
	MacAddr &= ~XTE_RCW1_PAUSEADDR_MASK;

	/* Set MAC bits [47:32] */
	MacAddr |= Aptr[4];
	MacAddr |= Aptr[5] << 8;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_RCW1_OFFSET, MacAddr);

	xdbg_printf(XDBG_DEBUG_GENERAL,
		   "XLlTemac_SetMacPauseAddress: returning SUCCESS\n");

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * XLlTemac_GetMacPauseAddress gets the MAC address used for pause frames for the
 * TEMAC channel specified by <i>InstancePtr</i>. 
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
void XLlTemac_GetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr)
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
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetMacPauseAddress\n");
	
	/* Read MAC bits [31:0] in ERXC0 */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_RCW0_OFFSET);
	Aptr[0] = (u8) MacAddr;
	Aptr[1] = (u8) (MacAddr >> 8);
	Aptr[2] = (u8) (MacAddr >> 16);
	Aptr[3] = (u8) (MacAddr >> 24);

	/* Read MAC bits [47:32] in RCW1 */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_RCW1_OFFSET);
	Aptr[4] = (u8) MacAddr;
	Aptr[5] = (u8) (MacAddr >> 8);
	
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetMacPauseAddress: done\n");
}

/*****************************************************************************/
/**
 * XLlTemac_SendPausePacket sends a pause packet with the value of
 * <i>PauseValue</i>.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param PauseValue is the pause value in units of 512 bit times.
 *
 * @return On successful completion, XLlTemac_SendPausePacket returns
 *         XST_SUCCESS. Otherwise, if the TEMAC channel is not started,
 *         XLlTemac_SendPausePacket returns XST_DEVICE_IS_STOPPED.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_SendPausePacket(XLlTemac *InstancePtr, u16 PauseValue)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetMacPauseAddress\n");

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_SendPausePacket: returning DEVICE_IS_STOPPED\n");
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Send flow control frame */
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_TPF_OFFSET,
			     (u32) PauseValue & XTE_TPF_TPFV_MASK);
	
	xdbg_printf(XDBG_DEBUG_GENERAL,
		   "XLlTemac_SendPausePacket: returning SUCCESS\n");
	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * XLlTemac_GetSgmiiStatus get the state of the link when using the SGMII media
 * interface.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param SpeedPtr references the location to store the result, which is the
 *        autonegotiated link speed in units of Mbits/sec, either 0, 10, 100,
 *        or 1000.
 *
 * @return On successful completion, XLlTemac_GetSgmiiStatus returns XST_SUCCESS.
 *         Otherwise, if TEMAC channel is not using an SGMII interface,
 *         XLlTemac_GetSgmiiStatus returns XST_NO_FEATURE.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_GetSgmiiStatus(XLlTemac *InstancePtr, u16 *SpeedPtr)
{
	int PhyType;
	u32 EgmicReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_GetSgmiiStatus\n");
	/* Make sure PHY is SGMII */
	PhyType = XLlTemac_GetPhysicalInterface(InstancePtr);
	if (PhyType != XTE_PHY_TYPE_SGMII) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_GetSgmiiStatus: returning NO_FEATURE\n");
		return (XST_NO_FEATURE);
	}

	/* Get the current contents of RGMII/SGMII config register */
	EgmicReg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_PHYC_OFFSET);

	/* Extract speed */
	switch (EgmicReg & XTE_PHYC_SGMIILINKSPEED_MASK) {
	case XTE_PHYC_SGLINKSPD_10:
		*SpeedPtr = 10;
		break;

	case XTE_PHYC_SGLINKSPD_100:
		*SpeedPtr = 100;
		break;

	case XTE_PHYC_SGLINKSPD_1000:
		*SpeedPtr = 1000;
		break;

	default:
		*SpeedPtr = 0;
	}
		
	xdbg_printf(XDBG_DEBUG_GENERAL,
		   "XLlTemac_GetSgmiiStatus: returning SUCCESS\n");
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * XLlTemac_GetRgmiiStatus get the state of the link when using the RGMII media
 * interface.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param SpeedPtr references the location to store the result, which is the
 *        autonegotiaged link speed in units of Mbits/sec, either 0, 10, 100,
 *        or 1000.
 * @param IsFullDuplexPtr references the value to set to indicate full duplex
 *        operation. XLlTemac_GetRgmiiStatus sets <i>IsFullDuplexPtr</i> to TRUE
 *        when the RGMII link is operating in full duplex mode. Otherwise,
 *        XLlTemac_GetRgmiiStatus sets <i>IsFullDuplexPtr</i> to FALSE.
 * @param IsLinkUpPtr references the value to set to indicate the link status.
 *        XLlTemac_GetRgmiiStatus sets <i>IsLinkUpPtr</i> to TRUE when the RGMII
 *        link up. Otherwise, XLlTemac_GetRgmiiStatus sets <i>IsLinkUpPtr</i> to
 *        FALSE.
 *
 * @return On successful completion, XLlTemac_GetRgmiiStatus returns XST_SUCCESS.
 *         Otherwise, if TEMAC channel is not using an RGMII interface,
 *         XLlTemac_GetRgmiiStatus returns XST_NO_FEATURE.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_GetRgmiiStatus(XLlTemac *InstancePtr, u16 *SpeedPtr,
			      int *IsFullDuplexPtr, int *IsLinkUpPtr)
{
	int PhyType;
	u32 EgmicReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
			XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK);
		
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_GetRgmiiStatus\n");
	/* Make sure PHY is RGMII */
	PhyType = XLlTemac_GetPhysicalInterface(InstancePtr);
	if ((PhyType != XTE_PHY_TYPE_RGMII_1_3) &&
	    (PhyType != XTE_PHY_TYPE_RGMII_2_0)) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			   "XLlTemac_GetRgmiiStatus: returning NO_FEATURE\n");
		return (XST_NO_FEATURE);
	}

	/* Get the current contents of RGMII/SGMII config register */
	EgmicReg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
			XTE_PHYC_OFFSET);

	/* Extract speed */
	switch (EgmicReg & XTE_PHYC_RGMIILINKSPEED_MASK) {
	case XTE_PHYC_RGLINKSPD_10:
		*SpeedPtr = 10;
		break;

	case XTE_PHYC_RGLINKSPD_100:
		*SpeedPtr = 100;
		break;

	case XTE_PHYC_RGLINKSPD_1000:
		*SpeedPtr = 1000;
		break;

	default:
		*SpeedPtr = 0;
	}

	/* Extract duplex and link status */
	if (EgmicReg & XTE_PHYC_RGMIIHD_MASK) {
		*IsFullDuplexPtr = FALSE;
	}
	else {
		*IsFullDuplexPtr = TRUE;
	}

	if (EgmicReg & XTE_PHYC_RGMIILINK_MASK) {
		*IsLinkUpPtr = TRUE;
	}
	else {
		*IsLinkUpPtr = FALSE;
	}

	xdbg_printf(XDBG_DEBUG_GENERAL,
		   "XLlTemac_GetRgmiiStatus: returning SUCCESS\n");
	return (XST_SUCCESS);
}


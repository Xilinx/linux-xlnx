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
 * @file xtemac_control.c
 *
 * Functions in this file implement general purpose command and control related
 * functionality. See xtemac.h for a detailed description of the driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a rmm  06/01/05 First release
 * 1.00b rmm  09/23/05 Implemented PhyRead/Write and multicast functions,
 *                     removed Set/Get IFG functions. Redesigned MII/RGMII/
 *                     SGMII status functions.
 * 2.00a rmm  11/21/05 Added auto negotiate to options processing funcs,
 *                     fixed XTE_MGTDR_OFFSET and XTE_MGTCR_OFFSET to be
 *                     accessed with IPIF instead of host macros, removed
 *                     half duplex option processing
 *       rmm  06/22/06 Fixed c++ compiler warnings and errors
 * </pre>
 *****************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac.h"
#include "xtemac_i.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
 * Set the MAC address for this driver/device.  The address is a 48-bit value.
 * The device must be stopped before calling this function.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param AddressPtr is a pointer to a 6-byte MAC address.
 *
 * @return
 * - XST_SUCCESS if the MAC address was set successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 *
 ******************************************************************************/
int XTemac_SetMacAddress(XTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Set the MAC bits [31:0] in EUAW0 */
	MacAddr = Aptr[0] & 0x000000FF;
	MacAddr |= Aptr[1] << 8;
	MacAddr |= Aptr[2] << 16;
	MacAddr |= Aptr[3] << 24;
	XTemac_mSetHostReg(XTE_UAW0_OFFSET, MacAddr);

	/* There are reserved bits in EUAW1 so don't affect them */
	MacAddr = XTemac_mGetHostReg(XTE_UAW1_OFFSET);
	MacAddr &= ~XTE_UAW1_MASK;

	/* Set MAC bits [47:32] in EUAW1 */
	MacAddr |= Aptr[4] & 0x000000FF;
	MacAddr |= Aptr[5] << 8;
	XTemac_mSetHostReg(XTE_UAW1_OFFSET, MacAddr);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Get the MAC address for this driver/device.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param AddressPtr is an output parameter, and is a pointer to a buffer into
 *        which the current MAC address will be copied. The buffer must be at
 *        least 6 bytes in length.
 *
 ******************************************************************************/
void XTemac_GetMacAddress(XTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Read MAC bits [31:0] in EUAW0 */
	MacAddr = XTemac_mGetHostReg(XTE_UAW0_OFFSET);
	Aptr[0] = (u8) MacAddr;
	Aptr[1] = (u8) (MacAddr >> 8);
	Aptr[2] = (u8) (MacAddr >> 16);
	Aptr[3] = (u8) (MacAddr >> 24);

	/* Read MAC bits [47:32] in EUAW1 */
	MacAddr = XTemac_mGetHostReg(XTE_UAW1_OFFSET);
	Aptr[4] = (u8) MacAddr;
	Aptr[5] = (u8) (MacAddr >> 8);
}


/*****************************************************************************/
/**
 * Add an Ethernet address to the list that will be accepted by the receiver.
 * The address may be any unicast, multicast, or the broadcast address form.
 * Up to XTE_MULTI_CAM_ENTRIES addresses may be filtered in this way. The
 * device must be stopped to use this function.
 *
 * Once an address is programmed, it will be received by the device. There is
 * no control bit to disable multicast filtering. The only way to prevent a
 * CAM address from being received is to clear it with XTemac_MulticastClear().
 *
 * @param InstancePtr is a pointer to the XTemac instance to be worked on.
 * @param AddressPtr is a pointer to a 6-byte Ethernet address. The previous
 *        address at this entry location (if any) is overwritten with the new
 *        one.
 * @param Entry is the storage location the HW uses to program this address.
 *        It must be between 0..XTE_MULTI_CAM_ENTRIES-1.
 *
 * @return
 *
 * - XST_SUCCESS if the address was added successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 ******************************************************************************/
int XTemac_MulticastAdd(XTemac *InstancePtr, void *AddressPtr, int Entry)
{
	u32 Emaw0Reg;
	u32 Emaw1Reg;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(Entry < XTE_MULTI_CAM_ENTRIES);

	/* The device must be stopped before clearing the multicast hash table */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Set MAC bits [31:0] */
	Emaw0Reg = Aptr[0] & 0x000000FF;
	Emaw0Reg |= Aptr[1] << 8;
	Emaw0Reg |= Aptr[2] << 16;
	Emaw0Reg |= Aptr[3] << 24;

	/* Set MAC bits [47:32] */
	Emaw1Reg = Aptr[4] & 0x000000FF;
	Emaw1Reg |= Aptr[5] << 8;

	/* Add in CAM address */
	Emaw1Reg |= (Entry << XTE_MAW1_CAMMADDR_SHIFT_MASK);

	/* Program HW */
	XTemac_mSetHostReg(XTE_MAW0_OFFSET, Emaw0Reg);
	XTemac_mSetHostReg(XTE_MAW1_OFFSET, Emaw1Reg);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Retrieve an Ethernet address set by XTemac_MulticastAdd().
 *
 * @param InstancePtr is a pointer to the XTemac instance to be worked on.
 * @param AddressPtr is an output parameter, and is a pointer to a buffer into
 *        which the current MAC address will be copied. The buffer must be at
 *        least 6 bytes in length.
 * @param Entry is the storage location in the HW. It must be between
 *        0..XTE_MULTI_CAM_ENTRIES-1.
 *
 * @return
 *
 * - XST_SUCCESS if the address was added successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 ******************************************************************************/
void XTemac_MulticastGet(XTemac *InstancePtr, void *AddressPtr, int Entry)
{
	u32 Emaw0Reg;
	u32 Emaw1Reg;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(Entry < XTE_MULTI_CAM_ENTRIES);

	/* Tell HW to provide address stored in given entry */
	XTemac_mSetHostReg(XTE_MAW1_OFFSET, XTE_MAW1_CAMRNW_MASK |
			   (Entry << XTE_MAW1_CAMMADDR_SHIFT_MASK));

	/* The HW should now have provided the CAM entry */
	Emaw0Reg = XTemac_mGetHostReg(XTE_MAW0_OFFSET);
	Emaw1Reg = XTemac_mGetHostReg(XTE_MAW1_OFFSET);

	/* Copy the address to the user buffer */
	Aptr[0] = (u8) Emaw0Reg;
	Aptr[1] = (u8) (Emaw0Reg >> 8);
	Aptr[2] = (u8) (Emaw0Reg >> 16);
	Aptr[3] = (u8) (Emaw0Reg >> 24);
	Aptr[4] = (u8) Emaw1Reg;
	Aptr[5] = (u8) (Emaw1Reg >> 8);
}

/*****************************************************************************/
/**
* Clear an address set by XTemac_MulticastAdd(). The device must be stopped
* before calling this function.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param Entry is the HW storage location used when this address was added.
*        It must be between 0..XTE_MULTI_CAM_ENTRIES-1.
*
* @return
*
* - XST_SUCCESS if the address was cleared
* - XST_DEVICE_IS_STARTED if the device has not yet been stopped
*
******************************************************************************/
int XTemac_MulticastClear(XTemac *InstancePtr, int Entry)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(Entry < XTE_MULTI_CAM_ENTRIES);

	/* The device must be stopped before clearing the multicast hash table */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Clear the entry by writing 0:0:0:0:0:0 to it */
	XTemac_mSetHostReg(XTE_MAW0_OFFSET, 0);
	XTemac_mSetHostReg(XTE_MAW1_OFFSET,
			   Entry << XTE_MAW1_CAMMADDR_SHIFT_MASK);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Set the MAC address for pause frames. This is the address the device will
 * recognize as pause frames. Pause frames transmitted with
 * XTemac_SendPausePacket() will also use this address.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param AddressPtr is a pointer to a 6-byte MAC address.
 *
 * @return
 * - XST_SUCCESS if the MAC address was set successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 *
 ******************************************************************************/
int XTemac_SetMacPauseAddress(XTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Set the MAC bits [31:0] in ERXC0 */
	MacAddr = Aptr[0] & 0x000000FF;
	MacAddr |= Aptr[1] << 8;
	MacAddr |= Aptr[2] << 16;
	MacAddr |= Aptr[3] << 24;
	XTemac_mSetHostReg(XTE_RXC0_OFFSET, MacAddr);

	/* ERXC1 contains other info that must be preserved */
	MacAddr = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	MacAddr &= ~XTE_RXC1_ERXC1_MASK;;

	/* Set MAC bits [47:32] */
	MacAddr |= Aptr[4] & 0x000000FF;
	MacAddr |= Aptr[5] << 8;
	XTemac_mSetHostReg(XTE_RXC1_OFFSET, MacAddr);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Get the MAC address for pause frames for this driver/device.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param AddressPtr is an output parameter, and is a pointer to a buffer into
 *        which the current MAC address will be copied. The buffer must be at
 *        least 6 bytes in length.
 *
 ******************************************************************************/
void XTemac_GetMacPauseAddress(XTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Read MAC bits [31:0] in ERXC0 */
	MacAddr = XTemac_mGetHostReg(XTE_RXC0_OFFSET);
	Aptr[0] = (u8) MacAddr;
	Aptr[1] = (u8) (MacAddr >> 8);
	Aptr[2] = (u8) (MacAddr >> 16);
	Aptr[3] = (u8) (MacAddr >> 24);

	/* Read MAC bits [47:32] in RXC1 */
	MacAddr = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	Aptr[4] = (u8) MacAddr;
	Aptr[5] = (u8) (MacAddr >> 8);
}


/*****************************************************************************/
/**
 * Set options for the driver/device. The driver should be stopped with
 * XTemac_Stop() before changing options.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Options are the options to set. Multiple options can be set by OR'ing
 *        XTE_*_OPTIONS constants together. Options not specified are not
 *        affected.
 *
 * @return
 * - XST_SUCCESS if the options were set successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 * - XST_NO_FEATURE if setting an option requires HW support not present
 *
 * @note
 * See xtemac.h for a description of the available options.
 *
 ******************************************************************************/
int XTemac_SetOptions(XTemac *InstancePtr, u32 Options)
{
	u32 Reg;		/* Generic register contents */
	u32 RegErxc1;		/* Reflects original contents of ERXC1 */
	u32 RegEtxc;		/* Reflects original contents of ETXC  */
	u32 RegNewErxc1;	/* Reflects new contents of ERXC1 */
	u32 RegNewEtxc;		/* Reflects new contents of ETXC  */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Polled mode requires FIFO direct */
	if ((Options & XTE_POLLED_OPTION) && (!XTemac_mIsFifo(InstancePtr))) {
		return (XST_NO_FEATURE);
	}

	/* Many of these options will change the ERXC1 or ETXC registers.
	 * To reduce the amount of IO to the device, group these options here
	 * and change them all at once.
	 */

	/* Grab current register contents */
	RegErxc1 = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	RegEtxc = XTemac_mGetHostReg(XTE_TXC_OFFSET);
	RegNewErxc1 = RegErxc1;
	RegNewEtxc = RegEtxc;

	/* Turn on jumbo packet support for both Rx and Tx */
	if (Options & XTE_JUMBO_OPTION) {
		RegNewEtxc |= XTE_TXC_TXJMBO_MASK;
		RegNewErxc1 |= XTE_RXC1_RXJMBO_MASK;
	}

	/* Turn on VLAN packet support for both Rx and Tx */
	if (Options & XTE_VLAN_OPTION) {
		RegNewEtxc |= XTE_TXC_TXVLAN_MASK;
		RegNewErxc1 |= XTE_RXC1_RXVLAN_MASK;
	}

	/* Turn on FCS stripping on receive packets */
	if (Options & XTE_FCS_STRIP_OPTION) {
		RegNewErxc1 &= ~XTE_RXC1_RXFCS_MASK;
	}

	/* Turn on FCS insertion on transmit packets */
	if (Options & XTE_FCS_INSERT_OPTION) {
		RegNewEtxc &= ~XTE_TXC_TXFCS_MASK;
	}

	/* Turn on length/type field checking on receive packets */
	if (Options & XTE_LENTYPE_ERR_OPTION) {
		RegNewErxc1 &= ~XTE_RXC1_RXLT_MASK;
	}

	/* Officially change the ETXC or ERXC1 registers if they need to be
	 * modified
	 */
	if (RegEtxc != RegNewEtxc) {
		XTemac_mSetHostReg(XTE_TXC_OFFSET, RegNewEtxc);
	}

	if (RegErxc1 != RegNewErxc1) {
		XTemac_mSetHostReg(XTE_RXC1_OFFSET, RegNewErxc1);
	}

	/* Rest of options twiddle bits of other registers. Handle them one at
	 * a time
	 */

	/* Turn on flow control */
	if (Options & XTE_FLOW_CONTROL_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_FCC_OFFSET);
		Reg |= XTE_FCC_RXFLO_MASK;
		XTemac_mSetHostReg(XTE_FCC_OFFSET, Reg);
	}

	/* Turn on promiscuous frame filtering (all frames are received ) */
	if (Options & XTE_PROMISC_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_AFM_OFFSET);
		Reg |= XTE_AFM_EPPRM_MASK;
		XTemac_mSetHostReg(XTE_AFM_OFFSET, Reg);
	}

	/* Allow broadcast address filtering */
	if (Options & XTE_BROADCAST_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_CR_OFFSET);
		Reg &= ~XTE_CR_BCREJ_MASK;
		XTemac_mSetIpifReg(XTE_CR_OFFSET, Reg);
	}

	/* Allow multicast address filtering */
	if (Options & XTE_MULTICAST_CAM_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_CR_OFFSET);
		Reg &= ~XTE_CR_MCREJ_MASK;
		XTemac_mSetIpifReg(XTE_CR_OFFSET, Reg);
	}

	/* Enable interrupts related to rejection of bad frames */
	if (Options & XTE_REPORT_RXERR_OPTION) {
		/* Clear out any previous error conditions that may have existed
		 * prior to enabling the reporting of these types of errors
		 */
		Reg = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
				   Reg & XTE_IPXR_RECV_DROPPED_MASK);

		/* Whether these are enabled here are based on the last call to
		 * XTemac_IntrFifoEnable/Disable() and XTemac_IntrSgDmaEnable/Disable()
		 * for the receive channel.
		 *
		 * If receive interrupts are enabled, then enable these interrupts. This
		 * way, when XTemac_Start() is called, these interrupt enables take
		 * effect right away.
		 *
		 * If receive interrupts are disabled, then don't do anything here. The
		 * XTemac_IntrFifoEnable() and XTemac_IntrSgDmaEnable() functions when
		 * called will check this option and enable these interrupts if needed.
		 */
		if (InstancePtr->Flags &
		    (XTE_FLAGS_RECV_FIFO_INT_ENABLE |
		     XTE_FLAGS_RECV_SGDMA_INT_ENABLE)) {
			Reg = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);
			Reg |= XTE_IPXR_RECV_DROPPED_MASK;
			XTemac_mSetIpifReg(XTE_IPIER_OFFSET, Reg);
		}
	}

	/* Enable interrrupt related to assertion of auto-negotiate HW interrupt */
	if (Options & XTE_ANEG_OPTION) {
		/* Clear out any previous interupt condition that may have existed
		 * prior to enabling the reporting of auto negotiation
		 */
		Reg = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
				   Reg & XTE_IPXR_AUTO_NEG_MASK);

		/* Make this interupt source enabled when XTemac_Start() is called */
		Reg = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);
		XTemac_mSetIpifReg(XTE_IPIER_OFFSET,
				   Reg & XTE_IPXR_AUTO_NEG_MASK);
	}

	/* Enable interrupts upon completing a SG list */
	if ((Options & XTE_SGEND_INT_OPTION) && XTemac_mIsSgDma(InstancePtr)) {
		Reg = XDmaV3_GetInterruptEnable(&InstancePtr->SendDma);
		Reg |= XDMAV3_IPXR_SGEND_MASK;
		XDmaV3_SetInterruptEnable(&InstancePtr->SendDma, Reg);

		Reg = XDmaV3_GetInterruptEnable(&InstancePtr->RecvDma);
		Reg |= XDMAV3_IPXR_SGEND_MASK;
		XDmaV3_SetInterruptEnable(&InstancePtr->RecvDma, Reg);
	}

	/* The remaining options not handled here are managed elsewhere in the
	 * driver. No register modifications are needed at this time. Reflecting the
	 * option in InstancePtr->Options is good enough for now.
	 */

	/* Set options word to its new value */
	InstancePtr->Options |= Options;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Clear options for the driver/device
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Options are the options to clear. Multiple options can be cleared by
 *        OR'ing XTE_*_OPTIONS constants together. Options not specified are not
 *        affected.
 *
 * @return
 * - XST_SUCCESS if the options were set successfully
 * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
 *
 * @note
 * See xtemac.h for a description of the available options.
 *
 ******************************************************************************/
int XTemac_ClearOptions(XTemac *InstancePtr, u32 Options)
{
	volatile u32 Reg;	/* Generic */
	u32 RegErxc1;		/* Reflects original contents of ERXC1 */
	u32 RegEtxc;		/* Reflects original contents of ETXC  */
	u32 RegNewErxc1;	/* Reflects new contents of ERXC1 */
	u32 RegNewEtxc;		/* Reflects new contents of ETXC  */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Many of these options will change the ERXC1 or ETXC registers.
	 * Group these options here and change them all at once. What we are
	 * trying to accomplish is to reduce the amount of IO to the device
	 */

	/* Grab current register contents */
	RegErxc1 = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	RegEtxc = XTemac_mGetHostReg(XTE_TXC_OFFSET);
	RegNewErxc1 = RegErxc1;
	RegNewEtxc = RegEtxc;

	/* Turn off jumbo packet support for both Rx and Tx */
	if (Options & XTE_JUMBO_OPTION) {
		RegNewEtxc &= ~XTE_TXC_TXJMBO_MASK;
		RegNewErxc1 &= ~XTE_RXC1_RXJMBO_MASK;
	}

	/* Turn off VLAN packet support for both Rx and Tx */
	if (Options & XTE_VLAN_OPTION) {
		RegNewEtxc &= ~XTE_TXC_TXVLAN_MASK;
		RegNewErxc1 &= ~XTE_RXC1_RXVLAN_MASK;
	}

	/* Turn off FCS stripping on receive packets */
	if (Options & XTE_FCS_STRIP_OPTION) {
		RegNewErxc1 |= XTE_RXC1_RXFCS_MASK;
	}

	/* Turn off FCS insertion on transmit packets */
	if (Options & XTE_FCS_INSERT_OPTION) {
		RegNewEtxc |= XTE_TXC_TXFCS_MASK;
	}

	/* Turn off length/type field checking on receive packets */
	if (Options & XTE_LENTYPE_ERR_OPTION) {
		RegNewErxc1 |= XTE_RXC1_RXLT_MASK;
	}

	/* Disable transmitter */
	if (Options & XTE_TRANSMITTER_ENABLE_OPTION) {
		RegNewEtxc &= ~XTE_TXC_TXEN_MASK;
	}

	/* Disable receiver */
	if (Options & XTE_RECEIVER_ENABLE_OPTION) {
		RegNewErxc1 &= ~XTE_RXC1_RXEN_MASK;
	}

	/* Officially change the ETXC or ERXC1 registers if they need to be
	 * modified
	 */
	if (RegEtxc != RegNewEtxc) {
		XTemac_mSetHostReg(XTE_TXC_OFFSET, RegNewEtxc);
	}

	if (RegErxc1 != RegNewErxc1) {
		XTemac_mSetHostReg(XTE_RXC1_OFFSET, RegNewErxc1);
	}

	/* Rest of options twiddle bits of other registers. Handle them one at
	 * a time
	 */

	/* Turn off flow control */
	if (Options & XTE_FLOW_CONTROL_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_FCC_OFFSET);
		Reg &= ~XTE_FCC_RXFLO_MASK;
		XTemac_mSetHostReg(XTE_FCC_OFFSET, Reg);
	}

	/* Turn off promiscuous frame filtering */
	if (Options & XTE_PROMISC_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_AFM_OFFSET);
		Reg &= ~XTE_AFM_EPPRM_MASK;
		XTemac_mSetHostReg(XTE_AFM_OFFSET, Reg);
	}

	/* Disable broadcast address filtering */
	if (Options & XTE_BROADCAST_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_CR_OFFSET);
		Reg |= XTE_CR_BCREJ_MASK;
		XTemac_mSetIpifReg(XTE_CR_OFFSET, Reg);
	}

	/* Disable multicast address filtering */
	if (Options & XTE_MULTICAST_CAM_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_CR_OFFSET);
		Reg |= XTE_CR_MCREJ_MASK;
		XTemac_mSetIpifReg(XTE_CR_OFFSET, Reg);
	}

	/* Disable interrupts related to rejection of bad frames */
	if (Options & XTE_REPORT_RXERR_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);
		Reg &= ~XTE_IPXR_RECV_DROPPED_MASK;
		XTemac_mSetIpifReg(XTE_IPIER_OFFSET, Reg);
	}

	/* Disable interrupts related to auto negotiate */
	if (Options & XTE_ANEG_OPTION) {
		Reg = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);
		Reg &= ~XTE_IPXR_AUTO_NEG_MASK;
		XTemac_mSetIpifReg(XTE_IPIER_OFFSET, Reg);
	}

	/* Disable interrupts upon completing a SG list */
	if ((Options & XTE_SGEND_INT_OPTION) && XTemac_mIsSgDma(InstancePtr)) {
		Reg = XDmaV3_GetInterruptEnable(&InstancePtr->SendDma);
		Reg &= ~XDMAV3_IPXR_SGEND_MASK;
		XDmaV3_SetInterruptEnable(&InstancePtr->SendDma, Reg);

		Reg = XDmaV3_GetInterruptEnable(&InstancePtr->RecvDma);
		Reg &= ~XDMAV3_IPXR_SGEND_MASK;
		XDmaV3_SetInterruptEnable(&InstancePtr->RecvDma, Reg);
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
 * Get current option settings
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return
 * A bitmask of XTE_*_OPTION constants. Any bit set to 1 is to be interpreted
 * as a set opion.
 *
 * @note
 * See xtemac.h for a description of the available options.
 *
 ******************************************************************************/
u32 XTemac_GetOptions(XTemac *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return (InstancePtr->Options);
}


/*****************************************************************************/
/**
 * Send a pause packet
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param PauseValue is the pause value in units of 512 bit times.
 *
 * @return
 * - XST_SUCCESS if pause frame transmission was initiated
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 *
 ******************************************************************************/
int XTemac_SendPausePacket(XTemac *InstancePtr, u16 PauseValue)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Send flow control frame */
	XTemac_mSetIpifReg(XTE_TPPR_OFFSET, (u32) PauseValue);
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Get the current operating link speed. This may be the value set by
 * XTemac_SetOperatingSpeed() or a HW default.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return Link speed in units of megabits per second
 *
 ******************************************************************************/
u16 XTemac_GetOperatingSpeed(XTemac *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	switch (XTemac_mGetHostReg(XTE_EMCFG_OFFSET) & XTE_EMCFG_LINKSPD_MASK) {
	case XTE_EMCFG_LINKSPD_1000:
		return (1000);

	case XTE_EMCFG_LINKSPD_100:
		return (100);

	case XTE_EMCFG_LINKSPD_10:
		return (10);

	default:
		return (0);
	}
}


/*****************************************************************************/
/**
 * Set the current operating link speed. For any traffic to be passed, this
 * speed must match the current MII/GMII/SGMII/RGMII link speed.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Speed is the speed to set in units of Mbps. Valid values are 10, 100,
 *        or 1000. Invalid values result in no change to the device.
 *
 ******************************************************************************/
void XTemac_SetOperatingSpeed(XTemac *InstancePtr, u16 Speed)
{
	u32 EcfgReg;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID((Speed == 10) || (Speed == 100) || (Speed == 1000));

	/* Get the current contents of the EMAC config register and zero out
	 * speed bits
	 */
	EcfgReg =
		XTemac_mGetHostReg(XTE_EMCFG_OFFSET) & ~XTE_EMCFG_LINKSPD_MASK;

	switch (Speed) {
	case 10:
		break;

	case 100:
		EcfgReg |= XTE_EMCFG_LINKSPD_100;
		break;

	case 1000:
		EcfgReg |= XTE_EMCFG_LINKSPD_1000;
		break;

	default:
		return;
	}

	/* Set register and return */
	XTemac_mSetHostReg(XTE_EMCFG_OFFSET, EcfgReg);
}

/*****************************************************************************/
/**
 * Get the current state of the link when media interface is of the SGMII type
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param SpeedPtr is a return value set to either 0, 10, 100, or 1000. Units
 *        are in Mbits/sec.
 *
 * @return
 *   - XST_SUCCESS if the SGMII status was read and return values set.
 *   - XST_NO_FEATURE if the device is not using SGMII.
 *
 ******************************************************************************/
int XTemac_GetSgmiiStatus(XTemac *InstancePtr, u16 *SpeedPtr)
{
	int PhyType;
	u32 EgmicReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Make sure PHY is SGMII */
	PhyType = XTemac_mGetPhysicalInterface(InstancePtr);
	if (PhyType != XTE_PHY_TYPE_SGMII) {
		return (XST_NO_FEATURE);
	}

	/* Get the current contents of RGMII/SGMII config register */
	EgmicReg = XTemac_mGetHostReg(XTE_GMIC_OFFSET);

	/* Extract speed */
	switch (EgmicReg & XTE_GMIC_RGLINKSPD_MASK) {
	case XTE_GMIC_RGLINKSPD_10:
		*SpeedPtr = 10;
		break;

	case XTE_GMIC_RGLINKSPD_100:
		*SpeedPtr = 100;
		break;

	case XTE_GMIC_RGLINKSPD_1000:
		*SpeedPtr = 1000;
		break;

	default:
		*SpeedPtr = 0;
	}

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Get the current state of the link when media interface is of the RGMII type
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param SpeedPtr is a return value set to either 0, 10, 100, or 1000. Units
 *        are in Mbits/sec.
 * @param IsFullDuplexPtr is a return value set to TRUE if the RGMII link
 *        is operating in full duplex, or FALSE if operating in half duplex.
 *        XTE_RGMII_LINK_UP.
 * @param IsLinkUpPtr is a return value set to TRUE if the RGMII link is up,
 *        or FALSE if the link is down.
 *
 * @return
 *   - XST_SUCCESS if the RGMII status was read and return values set.
 *   - XST_NO_FEATURE if the device is not using RGMII.
 *
 ******************************************************************************/
int XTemac_GetRgmiiStatus(XTemac *InstancePtr, u16 *SpeedPtr,
			  u32 *IsFullDuplexPtr, u32 *IsLinkUpPtr)
{
	int PhyType;
	u32 EgmicReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Make sure PHY is RGMII */
	PhyType = XTemac_mGetPhysicalInterface(InstancePtr);
	if ((PhyType != XTE_PHY_TYPE_RGMII_1_3) &&
	    (PhyType != XTE_PHY_TYPE_RGMII_2_0)) {
		return (XST_NO_FEATURE);
	}

	/* Get the current contents of RGMII/SGMII config register */
	EgmicReg = XTemac_mGetHostReg(XTE_GMIC_OFFSET);

	/* Extract speed */
	switch (EgmicReg & XTE_GMIC_RGLINKSPD_MASK) {
	case XTE_GMIC_RGLINKSPD_10:
		*SpeedPtr = 10;
		break;

	case XTE_GMIC_RGLINKSPD_100:
		*SpeedPtr = 100;
		break;

	case XTE_GMIC_RGLINKSPD_1000:
		*SpeedPtr = 1000;
		break;

	default:
		*SpeedPtr = 0;
	}

	/* Extract duplex and link status */
	if (EgmicReg & XTE_GMIC_RGHALFDUPLEX_MASK) {
		*IsFullDuplexPtr = FALSE;
	}
	else {
		*IsFullDuplexPtr = TRUE;
	}

	if (EgmicReg & XTE_GMIC_RGSTATUS_MASK) {
		*IsLinkUpPtr = TRUE;
	}
	else {
		*IsLinkUpPtr = FALSE;
	}

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Set the MDIO clock divisor. This function must be called once after each
 * reset prior to accessing MII PHY registers.
 *
 * Calculating the divisor:
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
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Divisor is the divisor to set. Range is 0 to XTE_MC_CLK_DVD_MAX.
 *
 ******************************************************************************/
void XTemac_PhySetMdioDivisor(XTemac *InstancePtr, u8 Divisor)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY)
		XASSERT_VOID(Divisor <= XTE_MC_CLK_DVD_MAX);

	XTemac_mSetHostReg(XTE_MC_OFFSET, (u32) Divisor | XTE_MC_MDIO_MASK);
}


/*****************************************************************************/
/*
*
* Read the current value of the PHY register indicated by the PhyAddress and
* the RegisterNum parameters. The MAC provides the driver with the ability to
* talk to a PHY that adheres to the Media Independent Interface (MII) as
* defined in the IEEE 802.3 standard.
*
* Prior to PHY access with this function, the user should have setup the MDIO
* clock with XTemac_PhySetMdioDivisor().
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param PhyAddress is the address of the PHY to be read (supports multiple
*        PHYs)
* @param RegisterNum is the register number, 0-31, of the specific PHY register
*        to read
* @param PhyDataPtr is an output parameter, and points to a 16-bit buffer into
*        which the current value of the register will be copied.
*
* @return
*
* - XST_SUCCESS if the PHY was read from successfully
* - XST_NO_FEATURE if the device is not configured with MII support
* - XST_EMAC_MII_BUSY if there is another PHY operation in progress
*
* @note
*
* This function is not thread-safe. The user must provide mutually exclusive
* access to this function if there are to be multiple threads that can call it.
* <br><br>
* There is the possibility that this function will not return if the hardware
* is broken (i.e., it never sets the status bit indicating that the read is
* done). If this is of concern to the user, the user should provide a mechanism
* suitable to their needs for recovery.
* <br><br>
* For the duration of this function, all host interface reads and writes are
* blocked to the current Temac instance and also the 2nd instance if it exists
* in the system. This is a HW limitation. See xtemac.h for a list of functions
* that will be blocked until this operation completes.
*
******************************************************************************/
int XTemac_PhyRead(XTemac *InstancePtr, u32 PhyAddress,
		   u32 RegisterNum, u16 *PhyDataPtr)
{
	u32 Mgtcr;
	volatile u32 Ipisr;

	XASSERT_NONVOID(InstancePtr != NULL);

	/* Make sure no other PHY operation is currently in progress */
	if (XTemac_mGetIpifReg(XTE_IPISR_OFFSET) & XTE_IPXR_MII_PEND_MASK) {
		return (XST_EMAC_MII_BUSY);
	}

	/* Construct Mgtcr mask for the operation */
	Mgtcr = RegisterNum & XTE_MGTCR_REGAD_MASK;
	Mgtcr |= ((PhyAddress << XTE_MGTCR_PHYAD_SHIFT_MASK) &
		  XTE_MGTCR_PHYAD_MASK);
	Mgtcr |= XTE_MGTCR_RWN_MASK;

	/* Write Mgtcr and wait for completion */
	XTemac_mSetIpifReg(XTE_MGTCR_OFFSET, Mgtcr);

	do {
		Ipisr = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	} while (!(Ipisr & XTE_IPXR_MII_DONE_MASK));

	/* Read data */
	*PhyDataPtr = XTemac_mGetIpifReg(XTE_MGTDR_OFFSET);

	/* Clear MII status bits */
	XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
			   Ipisr & (XTE_IPXR_MII_DONE_MASK |
				    XTE_IPXR_MII_PEND_MASK));

	return (XST_SUCCESS);
}


/*****************************************************************************/
/*
* Write data to the specified PHY register. The Ethernet driver does not
* require the device to be stopped before writing to the PHY.  Although it is
* probably a good idea to stop the device, it is the responsibility of the
* application to deem this necessary. The MAC provides the driver with the
* ability to talk to a PHY that adheres to the Media Independent Interface
* (MII) as defined in the IEEE 802.3 standard.
*
* Prior to PHY access with this function, the user should have setup the MDIO
* clock with XTemac_PhySetMdioDivisor().
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param PhyAddress is the address of the PHY to be written (supports multiple
*        PHYs)
* @param RegisterNum is the register number, 0-31, of the specific PHY register
*        to write
* @param PhyData is the 16-bit value that will be written to the register
*
* @return
*
* - XST_SUCCESS if the PHY was written to successfully. Since there is no error
*   status from the MAC on a write, the user should read the PHY to verify the
*   write was successful.
* - XST_NO_FEATURE if the device is not configured with MII support
* - XST_EMAC_MII_BUSY if there is another PHY operation in progress
*
* @note
*
* This function is not thread-safe. The user must provide mutually exclusive
* access to this function if there are to be multiple threads that can call it.
* <br><br>
* There is the possibility that this function will not return if the hardware
* is broken (i.e., it never sets the status bit indicating that the write is
* done). If this is of concern to the user, the user should provide a mechanism
* suitable to their needs for recovery.
* <br><br>
* For the duration of this function, all host interface reads and writes are
* blocked to the current Temac instance and also the 2nd instance if it exists
* in the system. This is a HW limitation. See xtemac.h for a list of functions
* that will be blocked until this operation completes.
*
******************************************************************************/
int XTemac_PhyWrite(XTemac *InstancePtr, u32 PhyAddress,
		    u32 RegisterNum, u16 PhyData)
{
	u32 Mgtcr;
	volatile u32 Ipisr;

	XASSERT_NONVOID(InstancePtr != NULL);

	/* Make sure no other PHY operation is currently in progress */
	if (XTemac_mGetIpifReg(XTE_IPISR_OFFSET) & XTE_IPXR_MII_PEND_MASK) {
		return (XST_EMAC_MII_BUSY);
	}

	/* Construct Mgtcr mask for the operation */
	Mgtcr = RegisterNum & XTE_MGTCR_REGAD_MASK;
	Mgtcr |= ((PhyAddress << XTE_MGTCR_PHYAD_SHIFT_MASK) &
		  XTE_MGTCR_PHYAD_MASK);

	/* Write Mgtdr and Mgtcr and wait for completion */
	XTemac_mSetIpifReg(XTE_MGTDR_OFFSET, (u32) PhyData);
	XTemac_mSetIpifReg(XTE_MGTCR_OFFSET, Mgtcr);

	do {
		Ipisr = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	} while (!(Ipisr & XTE_IPXR_MII_DONE_MASK));

	/* Clear MII status bits */
	XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
			   Ipisr & (XTE_IPXR_MII_DONE_MASK |
				    XTE_IPXR_MII_PEND_MASK));

	return (XST_SUCCESS);
}

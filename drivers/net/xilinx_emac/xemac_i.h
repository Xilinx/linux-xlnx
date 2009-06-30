/* $Id: xemac_i.h,v 1.2 2007/05/15 00:52:28 wre Exp $ */
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
*       (c) Copyright 2003 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemac_i.h
*
* This header file contains internal identifiers, which are those shared
* between XEmac components.  The identifiers in this file are not intended for
* use external to the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  07/31/01 First release
* 1.00b rpm  02/20/02 Repartitioned files and functions
* 1.00b rpm  04/29/02 Moved register definitions to xemac_hw.h
* 1.00c rpm  12/05/02 New version includes support for simple DMA
* 1.00d rpm  09/26/03 New version includes support PLB Ethernet and v2.00a of
*                     the packet fifo driver.
* 1.01a ecm  09/01/05 Added DRE support through separate SgSendDRE specific
*                     define, XEM_DRE_SEND_BD_MASK.
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XEMAC_I_H		/* prevent circular inclusions */
#define XEMAC_I_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xemac.h"
#include "xemac_hw.h"

/******************************************************************************
 *
 * Definitions transferred from the IPIF library header file.
 *
 ******************************************************************************/
/** @name Register Offsets
 *
 * The following constants define the register offsets for the registers of the
 * IPIF, there are some holes in the memory map for reserved addresses to allow
 * other registers to be added and still match the memory map of the interrupt
 * controller registers
 * @{
 */
#define XEMAC_DISR_OFFSET     0UL  /**< device interrupt status register */
#define XEMAC_DIPR_OFFSET     4UL  /**< device interrupt pending register */
#define XEMAC_DIER_OFFSET     8UL  /**< device interrupt enable register */
#define XEMAC_DIIR_OFFSET     24UL /**< device interrupt ID register */
#define XEMAC_DGIER_OFFSET    28UL /**< device global interrupt enable register */
#define XEMAC_IISR_OFFSET     32UL /**< IP interrupt status register */
#define XEMAC_IIER_OFFSET     40UL /**< IP interrupt enable register */
#define XEMAC_RESETR_OFFSET   64UL /**< reset register */
/* @} */

/**
 * The value used for the reset register to reset the IPIF
 */
#define XEMAC_RESET_MASK             0xAUL

/**
 * The following constant is used for the device global interrupt enable
 * register, to enable all interrupts for the device, this is the only bit
 * in the register
 */
#define XEMAC_GINTR_ENABLE_MASK      0x80000000UL

/**
 * The mask to identify each internal IPIF error condition in the device
 * registers of the IPIF. Interrupts are assigned in the register from LSB
 * to the MSB
 */
#define XEMAC_ERROR_MASK             1UL     /**< LSB of the register */

/** @name Interrupt IDs
 *
 * The interrupt IDs which identify each internal IPIF condition, this value
 * must correlate with the mask constant for the error
 * @{
 */
#define XEMAC_ERROR_INTERRUPT_ID     0    /**< interrupt bit #, (LSB = 0) */
#define XEMAC_NO_INTERRUPT_ID        128  /**< no interrupts are pending */
/* @} */

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/*****************************************************************************/
/**
*
* Reset the IPIF component and hardware.  This is a destructive operation that
* could cause the loss of data since resetting the IPIF of a device also
* resets the device using the IPIF and any blocks, such as FIFOs or DMA
* channels, within the IPIF.  All registers of the IPIF will contain their
* reset value when this function returns.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return   None
*
* @note     None
*
******************************************************************************/
#define XEMAC_RESET(RegBaseAddress) \
    XIo_Out32(RegBaseAddress + XEMAC_RESETR_OFFSET, XEMAC_RESET_MASK)

/*****************************************************************************/
/**
*
* This macro sets the device interrupt status register to the value.
* This register indicates the status of interrupt sources for a device
* which contains the IPIF.  The status is independent of whether interrupts
* are enabled and could be used for polling a device at a higher level rather
* than a more detailed level.
*
* Each bit of the register correlates to a specific interrupt source within the
* device which contains the IPIF.  With the exception of some internal IPIF
* conditions, the contents of this register are not latched but indicate
* the live status of the interrupt sources within the device.  Writing any of
* the non-latched bits of the register will have no effect on the register.
*
* For the latched bits of this register only, setting a bit which is zero
* within this register causes an interrupt to generated.  The device global
* interrupt enable register and the device interrupt enable register must be set
* appropriately to allow an interrupt to be passed out of the device. The
* interrupt is cleared by writing to this register with the bits to be
* cleared set to a one and all others to zero.  This register implements a
* toggle on write functionality meaning any bits which are set in the value
* written cause the bits in the register to change to the opposite state.
*
* This function writes the specified value to the register such that
* some bits may be set and others cleared.  It is the caller's responsibility
* to get the value of the register prior to setting the value to prevent a
* destructive behavior.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @param Status contains the value to be written to the interrupt status
*        register of the device.  The only bits which can be written are
*        the latched bits which contain the internal IPIF conditions.  The
*        following values may be used to set the status register or clear an
*        interrupt condition.
*        - XEMAC_ERROR_MASK     Indicates a device error in the IPIF
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
#define XEMAC_WRITE_DISR(RegBaseAddress, Status) \
    XIo_Out32((RegBaseAddress) + XEMAC_DISR_OFFSET, (Status))

/*****************************************************************************/
/**
*
* This macro gets the device interrupt status register contents.
* This register indicates the status of interrupt sources for a device
* which contains the IPIF.  The status is independent of whether interrupts
* are enabled and could be used for polling a device at a higher level.
*
* Each bit of the register correlates to a specific interrupt source within the
* device which contains the IPIF.  With the exception of some internal IPIF
* conditions, the contents of this register are not latched but indicate
* the live status of the interrupt sources within the device.
*
* For only the latched bits of this register, the interrupt may be cleared by
* writing to these bits in the status register.
*
* @param    RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* A status which contains the value read from the interrupt status register of
* the device. The bit definitions are specific to the device with
* the exception of the latched internal IPIF condition bits. The following
* values may be used to detect internal IPIF conditions in the status.
* <br><br>
* - XEMAC_ERROR_MASK     Indicates a device error in the IPIF
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_READ_DISR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_DISR_OFFSET)

/*****************************************************************************/
/**
*
* This function sets the device interrupt enable register contents.
* This register controls which interrupt sources of the device are allowed to
* generate an interrupt.  The device global interrupt enable register must also
* be set appropriately for an interrupt to be passed out of the device.
*
* Each bit of the register correlates to a specific interrupt source within the
* device which contains the IPIF.  Setting a bit in this register enables that
* interrupt source to generate an interrupt.  Clearing a bit in this register
* disables interrupt generation for that interrupt source.
*
* This function writes only the specified value to the register such that
* some interrupts source may be enabled and others disabled.  It is the
* caller's responsibility to get the value of the interrupt enable register
* prior to setting the value to prevent an destructive behavior.
*
* An interrupt source may not be enabled to generate an interrupt, but can
* still be polled in the interrupt status register.
*
* @param    RegBaseAddress contains the base address of the IPIF registers.
*
* @param
*
* Enable contains the value to be written to the interrupt enable register
* of the device.  The bit definitions are specific to the device with
* the exception of the internal IPIF conditions. The following
* values may be used to enable the internal IPIF conditions interrupts.
*   - XEMAC_ERROR_MASK     Indicates a device error in the IPIF
*
* @return
*
* None.
*
* @note
*
* Signature: Xuint32 XEMAC_WRITE_DIER(Xuint32 RegBaseAddress,
*                                          Xuint32 Enable)
*
******************************************************************************/
#define XEMAC_WRITE_DIER(RegBaseAddress, Enable) \
    XIo_Out32((RegBaseAddress) + XEMAC_DIER_OFFSET, (Enable))

/*****************************************************************************/
/**
*
* This function gets the device interrupt enable register contents.
* This register controls which interrupt sources of the device
* are allowed to generate an interrupt.  The device global interrupt enable
* register and the device interrupt enable register must also be set
* appropriately for an interrupt to be passed out of the device.
*
* Each bit of the register correlates to a specific interrupt source within the
* device which contains the IPIF.  Setting a bit in this register enables that
* interrupt source to generate an interrupt if the global enable is set
* appropriately.  Clearing a bit in this register disables interrupt generation
* for that interrupt source regardless of the global interrupt enable.
*
* @param    RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* The value read from the interrupt enable register of the device.  The bit
* definitions are specific to the device with the exception of the internal
* IPIF conditions. The following values may be used to determine from the
* value if the internal IPIF conditions interrupts are enabled.
* <br><br>
* - XEMAC_ERROR_MASK     Indicates a device error in the IPIF
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_READ_DIER(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_DIER_OFFSET)

/*****************************************************************************/
/**
*
* This function gets the device interrupt pending register contents.
* This register indicates the pending interrupt sources, those that are waiting
* to be serviced by the software, for a device which contains the IPIF.
* An interrupt must be enabled in the interrupt enable register of the IPIF to
* be pending.
*
* Each bit of the register correlates to a specific interrupt source within the
* the device which contains the IPIF.  With the exception of some internal IPIF
* conditions, the contents of this register are not latched since the condition
* is latched in the IP interrupt status register, by an internal block of the
* IPIF such as a FIFO or DMA channel, or by the IP of the device.  This register
* is read only and is not latched, but it is necessary to acknowledge (clear)
* the interrupt condition by performing the appropriate processing for the IP
* or block within the IPIF.
*
* This register can be thought of as the contents of the interrupt status
* register ANDed with the contents of the interrupt enable register.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* The value read from the interrupt pending register of the device.  The bit
* definitions are specific to the device with the exception of the latched
* internal IPIF condition bits. The following values may be used to detect
* internal IPIF conditions in the value.
* <br><br>
* - XEMAC_ERROR_MASK     Indicates a device error in the IPIF
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_READ_DIPR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_DIPR_OFFSET)

/*****************************************************************************/
/**
*
* This macro gets the device interrupt ID for the highest priority interrupt
* which is pending from the interrupt ID register. This function provides
* priority resolution such that faster interrupt processing is possible.
* Without priority resolution, it is necessary for the software to read the
* interrupt pending register and then check each interrupt source to determine
* if an interrupt is pending.  Priority resolution becomes more important as the
* number of interrupt sources becomes larger.
*
* Interrupt priorities are based upon the bit position of the interrupt in the
* interrupt pending register with bit 0 being the highest priority. The
* interrupt ID is the priority of the interrupt, 0 - 31, with 0 being the
* highest priority. The interrupt ID register is live rather than latched such
* that multiple calls to this function may not yield the same results.  A
* special value, outside of the interrupt priority range of 0 - 31, is
* contained in the register which indicates that no interrupt is pending.  This
* may be useful for allowing software to continue processing interrupts in a
* loop until there are no longer any interrupts pending.
*
* The interrupt ID is designed to allow a function pointer table to be used
* in the software such that the interrupt ID is used as an index into that
* table.  The function pointer table could contain an instance pointer, such
* as to DMA channel, and a function pointer to the function which handles
* that interrupt.  This design requires the interrupt processing of the device
* driver to be partitioned into smaller more granular pieces based upon
* hardware used by the device, such as DMA channels and FIFOs.
*
* It is not mandatory that this function be used by the device driver software.
* It may choose to read the pending register and resolve the pending interrupt
* priorities on it's own.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* An interrupt ID, 0 - 31, which identifies the highest priority interrupt
* which is pending.  A value of XIIF_NO_INTERRUPT_ID indicates that there is
* no interrupt pending. The following values may be used to identify the
* interrupt ID for the internal IPIF interrupts.
* <br><br>
* - XEMAC_ERROR_INTERRUPT_ID     Indicates a device error in the IPIF
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_READ_DIIR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_DIIR_OFFSET)

/*****************************************************************************/
/**
*
* This function disables all interrupts for the device by writing to the global
* interrupt enable register.  This register provides the ability to disable
* interrupts without any modifications to the interrupt enable register such
* that it is minimal effort to restore the interrupts to the previous enabled
* state.  The corresponding function, XIpIf_GlobalIntrEnable, is provided to
* restore the interrupts to the previous enabled state.  This function is
* designed to be used in critical sections of device drivers such that it is
* not necessary to disable other device interrupts.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
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
#define XEMAC_GINTR_DISABLE(RegBaseAddress) \
    XIo_Out32((RegBaseAddress) + XEMAC_DGIER_OFFSET, 0)

/*****************************************************************************/
/**
*
* This function writes to the global interrupt enable register to enable
* interrupts from the device.  This register provides the ability to enable
* interrupts without any modifications to the interrupt enable register such
* that it is minimal effort to restore the interrupts to the previous enabled
* state.  This function does not enable individual interrupts as the interrupt
* enable register must be set appropriately.  This function is designed to be
* used in critical sections of device drivers such that it is not necessary to
* disable other device interrupts.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
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
#define XEMAC_GINTR_ENABLE(RegBaseAddress)           \
    XIo_Out32((RegBaseAddress) + XEMAC_DGIER_OFFSET, \
               XEMAC_GINTR_ENABLE_MASK)

/*****************************************************************************/
/**
*
* This function determines if interrupts are enabled at the global level by
* reading the global interrupt register. This register provides the ability to
* disable interrupts without any modifications to the interrupt enable register
* such that it is minimal effort to restore the interrupts to the previous
* enabled state.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* XTRUE if interrupts are enabled for the IPIF, XFALSE otherwise.
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_IS_GINTR_ENABLED(RegBaseAddress)             \
    (XIo_In32((RegBaseAddress) + XEMAC_DGIER_OFFSET) ==    \
              XEMAC_GINTR_ENABLE_MASK)

/*****************************************************************************/
/**
*
* This function sets the IP interrupt status register to the specified value.
* This register indicates the status of interrupt sources for the IP of the
* device.  The IP is defined as the part of the device that connects to the
* IPIF.  The status is independent of whether interrupts are enabled such that
* the status register may also be polled when interrupts are not enabled.
*
* Each bit of the register correlates to a specific interrupt source within the
* IP.  All bits of this register are latched. Setting a bit which is zero
* within this register causes an interrupt to be generated.  The device global
* interrupt enable register and the device interrupt enable register must be set
* appropriately to allow an interrupt to be passed out of the device. The
* interrupt is cleared by writing to this register with the bits to be
* cleared set to a one and all others to zero.  This register implements a
* toggle on write functionality meaning any bits which are set in the value
* written cause the bits in the register to change to the opposite state.
*
* This function writes only the specified value to the register such that
* some status bits may be set and others cleared.  It is the caller's
* responsibility to get the value of the register prior to setting the value
* to prevent an destructive behavior.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @param Status contains the value to be written to the IP interrupt status
*        register.  The bit definitions are specific to the device IP.
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
#define XEMAC_WRITE_IISR(RegBaseAddress, Status) \
    XIo_Out32((RegBaseAddress) + XEMAC_IISR_OFFSET, (Status))

/*****************************************************************************/
/**
*
* This macro gets the contents of the IP interrupt status register.
* This register indicates the status of interrupt sources for the IP of the
* device.  The IP is defined as the part of the device that connects to the
* IPIF. The status is independent of whether interrupts are enabled such
* that the status register may also be polled when interrupts are not enabled.
*
* Each bit of the register correlates to a specific interrupt source within the
* device.  All bits of this register are latched.  Writing a 1 to a bit within
* this register causes an interrupt to be generated if enabled in the interrupt
* enable register and the global interrupt enable is set.  Since the status is
* latched, each status bit must be acknowledged in order for the bit in the
* status register to be updated.  Each bit can be acknowledged by writing a
* 0 to the bit in the status register.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* A status which contains the value read from the IP interrupt status register.
* The bit definitions are specific to the device IP.
*
* @note
*
* None.
*
******************************************************************************/
#define XEMAC_READ_IISR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_IISR_OFFSET)

/*****************************************************************************/
/**
*
* This macro sets the IP interrupt enable register contents.  This register
* controls which interrupt sources of the IP are allowed to generate an
* interrupt.  The global interrupt enable register and the device interrupt
* enable register must also be set appropriately for an interrupt to be
* passed out of the device containing the IPIF and the IP.
*
* Each bit of the register correlates to a specific interrupt source within the
* IP.  Setting a bit in this register enables the interrupt source to generate
* an interrupt.  Clearing a bit in this register disables interrupt generation
* for that interrupt source.
*
* This function writes only the specified value to the register such that
* some interrupt sources may be enabled and others disabled.  It is the
* caller's responsibility to get the value of the interrupt enable register
* prior to setting the value to prevent an destructive behavior.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @param Enable contains the value to be written to the IP interrupt enable
*        register. The bit definitions are specific to the device IP.
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
#define XEMAC_WRITE_IIER(RegBaseAddress, Enable) \
    XIo_Out32((RegBaseAddress) + XEMAC_IIER_OFFSET, (Enable))

/*****************************************************************************/
/**
*
* This macro gets the IP interrupt enable register contents.  This register
* controls which interrupt sources of the IP are allowed to generate an
* interrupt.  The global interrupt enable register and the device interrupt
* enable register must also be set appropriately for an interrupt to be
* passed out of the device containing the IPIF and the IP.
*
* Each bit of the register correlates to a specific interrupt source within the
* IP.  Setting a bit in this register enables the interrupt source to generate
* an interrupt.  Clearing a bit in this register disables interrupt generation
* for that interrupt source.
*
* @param RegBaseAddress contains the base address of the IPIF registers.
*
* @return
*
* The contents read from the IP interrupt enable register.  The bit definitions
* are specific to the device IP.
*
* @note
*
* Signature: Xuint32 XEMAC_READ_IIER(Xuint32 RegBaseAddress)
*
******************************************************************************/
#define XEMAC_READ_IIER(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XEMAC_IIER_OFFSET)


/******************************************************************************
 *
 * End of transferred IPIF definitions.
 *
 ******************************************************************************/

/************************** Constant Definitions *****************************/

/*
 * Default buffer descriptor control word masks. The default send BD control
 * is set for incrementing the source address by one for each byte transferred,
 * and specify that the destination address (FIFO) is local to the device. The
 * default receive BD control is set for incrementing the destination address
 * by one for each byte transferred, and specify that the source address is
 * local to the device.
 */
#define XEM_DFT_SEND_BD_MASK    (XDC_DMACR_SOURCE_INCR_MASK | \
                                 XDC_DMACR_DEST_LOCAL_MASK)
#define XEM_DFT_RECV_BD_MASK    (XDC_DMACR_DEST_INCR_MASK |  \
                                 XDC_DMACR_SOURCE_LOCAL_MASK)

/*
 * Masks for the IPIF Device Interrupt enable and status registers.
 */
#define XEM_IPIF_EMAC_MASK      0x00000004UL	/* MAC interrupt */
#define XEM_IPIF_SEND_DMA_MASK  0x00000008UL	/* Send DMA interrupt */
#define XEM_IPIF_RECV_DMA_MASK  0x00000010UL	/* Receive DMA interrupt */
#define XEM_IPIF_RECV_FIFO_MASK 0x00000020UL	/* Receive FIFO interrupt */
#define XEM_IPIF_SEND_FIFO_MASK 0x00000040UL	/* Send FIFO interrupt */

/*
 * Default IPIF Device Interrupt mask when configured for DMA
 */
#define XEM_IPIF_DMA_DFT_MASK   (XEM_IPIF_SEND_DMA_MASK |   \
                                 XEM_IPIF_RECV_DMA_MASK |   \
                                 XEM_IPIF_EMAC_MASK |       \
                                 XEM_IPIF_SEND_FIFO_MASK |  \
                                 XEM_IPIF_RECV_FIFO_MASK)

/*
 * Default IPIF Device Interrupt mask when configured without DMA
 */
#define XEM_IPIF_FIFO_DFT_MASK  (XEM_IPIF_EMAC_MASK |       \
                                 XEM_IPIF_SEND_FIFO_MASK |  \
                                 XEM_IPIF_RECV_FIFO_MASK)

#define XEM_IPIF_DMA_DEV_INTR_COUNT   7	/* Number of interrupt sources */
#define XEM_IPIF_FIFO_DEV_INTR_COUNT  5	/* Number of interrupt sources */
#define XEM_IPIF_DEVICE_INTR_COUNT  7	/* Number of interrupt sources */
#define XEM_IPIF_IP_INTR_COUNT      22	/* Number of MAC interrupts */


/* a mask for all transmit interrupts, used in polled mode */
#define XEM_EIR_XMIT_ALL_MASK   (XEM_EIR_XMIT_DONE_MASK |           \
                                 XEM_EIR_XMIT_ERROR_MASK |          \
                                 XEM_EIR_XMIT_SFIFO_EMPTY_MASK |    \
                                 XEM_EIR_XMIT_LFIFO_FULL_MASK)

/* a mask for all receive interrupts, used in polled mode */
#define XEM_EIR_RECV_ALL_MASK   (XEM_EIR_RECV_DONE_MASK |           \
                                 XEM_EIR_RECV_ERROR_MASK |          \
                                 XEM_EIR_RECV_LFIFO_EMPTY_MASK |    \
                                 XEM_EIR_RECV_LFIFO_OVER_MASK |     \
                                 XEM_EIR_RECV_LFIFO_UNDER_MASK |    \
                                 XEM_EIR_RECV_DFIFO_OVER_MASK |     \
                                 XEM_EIR_RECV_MISSED_FRAME_MASK |   \
                                 XEM_EIR_RECV_COLLISION_MASK |      \
                                 XEM_EIR_RECV_FCS_ERROR_MASK |      \
                                 XEM_EIR_RECV_LEN_ERROR_MASK |      \
                                 XEM_EIR_RECV_SHORT_ERROR_MASK |    \
                                 XEM_EIR_RECV_LONG_ERROR_MASK |     \
                                 XEM_EIR_RECV_ALIGN_ERROR_MASK)

/* a default interrupt mask for scatter-gather DMA operation */
#define XEM_EIR_DFT_SG_MASK    (XEM_EIR_RECV_ERROR_MASK |           \
                                XEM_EIR_RECV_LFIFO_OVER_MASK |      \
                                XEM_EIR_RECV_LFIFO_UNDER_MASK |     \
                                XEM_EIR_XMIT_SFIFO_OVER_MASK |      \
                                XEM_EIR_XMIT_SFIFO_UNDER_MASK |     \
                                XEM_EIR_XMIT_LFIFO_OVER_MASK |      \
                                XEM_EIR_XMIT_LFIFO_UNDER_MASK |     \
                                XEM_EIR_RECV_DFIFO_OVER_MASK |      \
                                XEM_EIR_RECV_MISSED_FRAME_MASK |    \
                                XEM_EIR_RECV_COLLISION_MASK |       \
                                XEM_EIR_RECV_FCS_ERROR_MASK |       \
                                XEM_EIR_RECV_LEN_ERROR_MASK |       \
                                XEM_EIR_RECV_SHORT_ERROR_MASK |     \
                                XEM_EIR_RECV_LONG_ERROR_MASK |      \
                                XEM_EIR_RECV_ALIGN_ERROR_MASK)

/* a default interrupt mask for non-DMA operation (direct FIFOs) */
#define XEM_EIR_DFT_FIFO_MASK  (XEM_EIR_XMIT_DONE_MASK |            \
                                XEM_EIR_RECV_DONE_MASK |            \
                                XEM_EIR_DFT_SG_MASK)


/*
 * Mask for the DMA interrupt enable and status registers when configured
 * for scatter-gather DMA.
 */
#define XEM_DMA_SG_INTR_MASK    (XDC_IXR_DMA_ERROR_MASK  |      \
                                 XDC_IXR_PKT_THRESHOLD_MASK |   \
                                 XDC_IXR_PKT_WAIT_BOUND_MASK |  \
                                 XDC_IXR_SG_END_MASK)


/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/


/*****************************************************************************/
/*
*
* Clears a structure of given size, in bytes, by setting each byte to 0.
*
* @param StructPtr is a pointer to the structure to be cleared.
* @param NumBytes is the number of bytes in the structure.
*
* @return
*
* None.
*
* @note
*
* Signature: void XEmac_mClearStruct(u8 *StructPtr, unsigned int NumBytes)
*
******************************************************************************/
#define XEmac_mClearStruct(StructPtr, NumBytes)     \
{                                                   \
    u32 i;                                          \
    u8 *BytePtr = (u8 *)(StructPtr);        \
    for (i=0; i < (unsigned int)(NumBytes); i++)    \
    {                                               \
        *BytePtr++ = 0;                             \
    }                                               \
}

/************************** Variable Definitions *****************************/

extern XEmac_Config XEmac_ConfigTable[];

/************************** Function Prototypes ******************************/

void XEmac_CheckEmacError(XEmac * InstancePtr, u32 IntrStatus);
void XEmac_CheckFifoRecvError(XEmac * InstancePtr);
void XEmac_CheckFifoSendError(XEmac * InstancePtr);


#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

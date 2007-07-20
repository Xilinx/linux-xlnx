/* $Id: xspi_hw.h,v 1.2 2007/04/13 00:55:47 wre Exp $ */
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
*       (c) Copyright 2002 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xspi_hw.h
*
* This header file contains identifiers and low-level driver functions (or
* macros) that can be used to access the device.  High-level driver functions
* are defined in xspi.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00b rpm  04/24/02 First release
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XSPI_HW_H		/* prevent circular inclusions */
#define XSPI_HW_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"

/************************** Constant Definitions *****************************/

/* the following constants define the register offsets for the registers of the
 * IPIF, there are some holes in the memory map for reserved addresses to allow
 * other registers to be added and still match the memory map of the interrupt
 * controller registers
 */
#define XSPI_DISR_OFFSET     0UL  /* device interrupt status register */
#define XSPI_DIPR_OFFSET     4UL  /* device interrupt pending register */
#define XSPI_DIER_OFFSET     8UL  /* device interrupt enable register */
#define XSPI_DIIR_OFFSET     24UL /* device interrupt ID register */
#define XSPI_DGIER_OFFSET    28UL /* device global interrupt enable reg */
#define XSPI_IISR_OFFSET     32UL /* IP interrupt status register */
#define XSPI_IIER_OFFSET     40UL /* IP interrupt enable register */
#define XSPI_RESETR_OFFSET   64UL /* reset register */


#define XSPI_RESET_MASK             0xAUL

/* the following constant is used for the device global interrupt enable
 * register, to enable all interrupts for the device, this is the only bit
 * in the register
 */
#define XSPI_GINTR_ENABLE_MASK      0x80000000UL

/* the following constants contain the masks to identify each internal IPIF
 * condition in the device registers of the IPIF, interrupts are assigned
 * in the register from LSB to the MSB
 */
#define XSPI_ERROR_MASK             1UL     /* LSB of the register */

/* The following constants contain interrupt IDs which identify each internal
 * IPIF condition, this value must correlate with the mask constant for the
 * error
 */
#define XSPI_ERROR_INTERRUPT_ID     0    /* interrupt bit #, (LSB = 0) */
#define XSPI_NO_INTERRUPT_ID        128  /* no interrupts are pending */

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/******************************************************************************
*
* MACRO:
*
* XSPI_RESET
*
* DESCRIPTION:
*
* Reset the IPIF component and hardware.  This is a destructive operation that
* could cause the loss of data since resetting the IPIF of a device also
* resets the device using the IPIF and any blocks, such as FIFOs or DMA
* channels, within the IPIF.  All registers of the IPIF will contain their
* reset value when this function returns.
*
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/

/* the following constant is used in the reset register to cause the IPIF to
 * reset
 */
#define XSPI_RESET(RegBaseAddress) \
    XIo_Out32(RegBaseAddress + XSPI_RESETR_OFFSET, XSPI_RESET_MASK)

/******************************************************************************
*
* MACRO:
*
* XSPI_WRITE_DISR
*
* DESCRIPTION:
*
* This function sets the device interrupt status register to the value.
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* Status contains the value to be written to the interrupt status register of
* the device.  The only bits which can be written are the latched bits which
* contain the internal IPIF conditions.  The following values may be used to
* set the status register or clear an interrupt condition.
*
*   XSPI_ERROR_MASK     Indicates a device error in the IPIF
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_WRITE_DISR(RegBaseAddress, Status) \
    XIo_Out32((RegBaseAddress) + XSPI_DISR_OFFSET, (Status))

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_DISR
*
* DESCRIPTION:
*
* This function gets the device interrupt status register contents.
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* A status which contains the value read from the interrupt status register of
* the device. The bit definitions are specific to the device with
* the exception of the latched internal IPIF condition bits. The following
* values may be used to detect internal IPIF conditions in the status.
*
*   XSPI_ERROR_MASK     Indicates a device error in the IPIF
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_READ_DISR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_DISR_OFFSET)

/******************************************************************************
*
* MACRO:
*
* XSPI_WRITE_DIER
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* Enable contains the value to be written to the interrupt enable register
* of the device.  The bit definitions are specific to the device with
* the exception of the internal IPIF conditions. The following
* values may be used to enable the internal IPIF conditions interrupts.
*
*   XSPI_ERROR_MASK     Indicates a device error in the IPIF
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* Signature: u32 XSPI_WRITE_DIER(u32 RegBaseAddress,
*                                         u32 Enable)
*
******************************************************************************/
#define XSPI_WRITE_DIER(RegBaseAddress, Enable) \
    XIo_Out32((RegBaseAddress) + XSPI_DIER_OFFSET, (Enable))

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_DIER
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* The value read from the interrupt enable register of the device.  The bit
* definitions are specific to the device with the exception of the internal
* IPIF conditions. The following values may be used to determine from the
* value if the internal IPIF conditions interrupts are enabled.
*
*   XSPI_ERROR_MASK     Indicates a device error in the IPIF
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_READ_DIER(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_DIER_OFFSET)

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_DIPR
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* The value read from the interrupt pending register of the device.  The bit
* definitions are specific to the device with the exception of the latched
* internal IPIF condition bits. The following values may be used to detect
* internal IPIF conditions in the value.
*
*   XSPI_ERROR_MASK     Indicates a device error in the IPIF
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_READ_DIPR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_DIPR_OFFSET)

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_DIIR
*
* DESCRIPTION:
*
* This function gets the device interrupt ID for the highest priority interrupt
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* An interrupt ID, 0 - 31, which identifies the highest priority interrupt
* which is pending.  A value of XIIF_NO_INTERRUPT_ID indicates that there is
* no interrupt pending. The following values may be used to identify the
* interrupt ID for the internal IPIF interrupts.
*
*   XSPI_ERROR_INTERRUPT_ID     Indicates a device error in the IPIF
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_READ_DIIR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_DIIR_OFFSET)

/******************************************************************************
*
* MACRO:
*
* XSPI_GLOBAL_INTR_DISABLE
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_GINTR_DISABLE(RegBaseAddress) \
    XIo_Out32((RegBaseAddress) + XSPI_DGIER_OFFSET, 0)

/******************************************************************************
*
* MACRO:
*
* XSPI_GINTR_ENABLE
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_GINTR_ENABLE(RegBaseAddress)           \
    XIo_Out32((RegBaseAddress) + XSPI_DGIER_OFFSET, \
               XSPI_GINTR_ENABLE_MASK)

/******************************************************************************
*
* MACRO:
*
* XSPI_IS_GINTR_ENABLED
*
* DESCRIPTION:
*
* This function determines if interrupts are enabled at the global level by
* reading the gloabl interrupt register. This register provides the ability to
* disable interrupts without any modifications to the interrupt enable register
* such that it is minimal effort to restore the interrupts to the previous
* enabled state.
*
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* TRUE if interrupts are enabled for the IPIF, FALSE otherwise.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_IS_GINTR_ENABLED(RegBaseAddress)             \
    (XIo_In32((RegBaseAddress) + XSPI_DGIER_OFFSET) ==    \
              XSPI_GINTR_ENABLE_MASK)

/******************************************************************************
*
* MACRO:
*
* XSPI_WRITE_IISR
*
* DESCRIPTION:
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* Status contains the value to be written to the IP interrupt status
* register.  The bit definitions are specific to the device IP.
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_WRITE_IISR(RegBaseAddress, Status) \
    XIo_Out32((RegBaseAddress) + XSPI_IISR_OFFSET, (Status))

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_IISR
*
* DESCRIPTION:
*
* This function gets the contents of the IP interrupt status register.
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

* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* A status which contains the value read from the IP interrupt status register.
* The bit definitions are specific to the device IP.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_READ_IISR(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_IISR_OFFSET)

/******************************************************************************
*
* MACRO:
*
* XSPI_WRITE_IIER
*
* DESCRIPTION:
*
* This function sets the IP interrupt enable register contents.  This register
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* Enable contains the value to be written to the IP interrupt enable register.
* The bit definitions are specific to the device IP.
*
* RETURN VALUE:
*
* None.
*
* NOTES:
*
* None.
*
******************************************************************************/
#define XSPI_WRITE_IIER(RegBaseAddress, Enable) \
    XIo_Out32((RegBaseAddress) + XSPI_IIER_OFFSET, (Enable))

/******************************************************************************
*
* MACRO:
*
* XSPI_READ_IIER
*
* DESCRIPTION:
*
*
* This function gets the IP interrupt enable register contents.  This register
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
* ARGUMENTS:
*
* RegBaseAddress contains the base address of the IPIF registers.
*
* RETURN VALUE:
*
* The contents read from the IP interrupt enable register.  The bit definitions
* are specific to the device IP.
*
* NOTES:
*
* Signature: u32 XSPI_READ_IIER(u32 RegBaseAddress)
*
******************************************************************************/
#define XSPI_READ_IIER(RegBaseAddress) \
    XIo_In32((RegBaseAddress) + XSPI_IIER_OFFSET)


/************************** Constant Definitions *****************************/

/*
 * Offset from the device base address (IPIF) to the IP registers.
 */
#define XSP_REGISTER_OFFSET      0x60

/*
 * Register offsets for the SPI. Each register except the CR & SSR is 8 bits,
 * so add 3 to the word-offset to get the LSB (in a big-endian system).
 */
#define XSP_CR_OFFSET   (XSP_REGISTER_OFFSET + 0x2)	/* 16-bit Control */
#define XSP_SR_OFFSET   (XSP_REGISTER_OFFSET + 0x4 + 3)	/* Status */
#define XSP_DTR_OFFSET  (XSP_REGISTER_OFFSET + 0x8 + 3)	/* Data transmit */
#define XSP_DRR_OFFSET  (XSP_REGISTER_OFFSET + 0xC + 3)	/* Data receive */
#define XSP_SSR_OFFSET  (XSP_REGISTER_OFFSET + 0x10)	/* 32-bit slave select */
#define XSP_TFO_OFFSET  (XSP_REGISTER_OFFSET + 0x14 + 3)	/* Transmit FIFO occupancy */
#define XSP_RFO_OFFSET  (XSP_REGISTER_OFFSET + 0x18 + 3)	/* Receive FIFO occupancy */

/*
 * SPI Control Register (CR) masks
 */
#define XSP_CR_LOOPBACK_MASK        0x1	/* Local loopback mode */
#define XSP_CR_ENABLE_MASK          0x2	/* System enable */
#define XSP_CR_MASTER_MODE_MASK     0x4	/* Enable master mode */
#define XSP_CR_CLK_POLARITY_MASK    0x8	/* Clock polarity high or low */
#define XSP_CR_CLK_PHASE_MASK      0x10	/* Clock phase 0 or 1 */
#define XSP_CR_TXFIFO_RESET_MASK   0x20	/* Reset transmit FIFO */
#define XSP_CR_RXFIFO_RESET_MASK   0x40	/* Reset receive FIFO */
#define XSP_CR_MANUAL_SS_MASK      0x80	/* Manual slave select assertion */
#define XSP_CR_TRANS_INHIBIT_MASK  0x100	/* Master transaction inhibit */

/*
 * SPI Status Register (SR) masks
 */
#define XSP_SR_RX_EMPTY_MASK        0x1	/* Receive register/FIFO is empty */
#define XSP_SR_RX_FULL_MASK         0x2	/* Receive register/FIFO is full */
#define XSP_SR_TX_EMPTY_MASK        0x4	/* Transmit register/FIFO is empty */
#define XSP_SR_TX_FULL_MASK         0x8	/* Transmit register/FIFO is full */
#define XSP_SR_MODE_FAULT_MASK     0x10	/* Mode fault error */

/*
 * SPI Transmit FIFO Occupancy (TFO) mask. The binary value plus one yields
 * the occupancy.
 */
#define XSP_TFO_MASK        0x1F

/*
 * SPI Receive FIFO Occupancy (RFO) mask. The binary value plus one yields
 * the occupancy.
 */
#define XSP_RFO_MASK        0x1F


/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/


/*****************************************************************************
*
* Low-level driver macros.  The list below provides signatures to help the
* user use the macros.
*
* void XSpi_mSetControlReg(u32 BaseAddress, u16 Mask)
* u16 XSpi_mGetControlReg(u32 BaseAddress)
* u8 XSpi_mGetStatusReg(u32 BaseAddress)
*
* void XSpi_mSetSlaveSelectReg(u32 BaseAddress, u32 Mask)
* u32 XSpi_mGetSlaveSelectReg(u32 BaseAddress)
*
* void XSpi_mEnable(u32 BaseAddress)
* void XSpi_mDisable(u32 BaseAddress)
*
* void XSpi_mSendByte(u32 BaseAddress, u8 Data);
* u8 XSpi_mRecvByte(u32 BaseAddress);
*
*****************************************************************************/

/****************************************************************************/
/**
*
* Set the contents of the control register. Use the XSP_CR_* constants defined
* above to create the bit-mask to be written to the register.
*
* @param    BaseAddress is the base address of the device
* @param    Mask is the 16-bit value to write to the control register
*
* @return   None.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mSetControlReg(BaseAddress, Mask) \
                    XIo_Out16((BaseAddress) + XSP_CR_OFFSET, (Mask))


/****************************************************************************/
/**
*
* Get the contents of the control register. Use the XSP_CR_* constants defined
* above to interpret the bit-mask returned.
*
* @param    BaseAddress is the  base address of the device
*
* @return   A 16-bit value representing the contents of the control register.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mGetControlReg(BaseAddress) \
                    XIo_In16((BaseAddress) + XSP_CR_OFFSET)


/****************************************************************************/
/**
*
* Get the contents of the status register. Use the XSP_SR_* constants defined
* above to interpret the bit-mask returned.
*
* @param    BaseAddress is the  base address of the device
*
* @return   An 8-bit value representing the contents of the status register.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mGetStatusReg(BaseAddress) \
                    XIo_In8((BaseAddress) + XSP_SR_OFFSET)


/****************************************************************************/
/**
*
* Set the contents of the slave select register. Each bit in the mask
* corresponds to a slave select line. Only one slave should be selected at
* any one time.
*
* @param    BaseAddress is the  base address of the device
* @param    Mask is the 32-bit value to write to the slave select register
*
* @return   None.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mSetSlaveSelectReg(BaseAddress, Mask) \
                    XIo_Out32((BaseAddress) + XSP_SSR_OFFSET, (Mask))


/****************************************************************************/
/**
*
* Get the contents of the slave select register. Each bit in the mask
* corresponds to a slave select line. Only one slave should be selected at
* any one time.
*
* @param    BaseAddress is the  base address of the device
*
* @return   The 32-bit value in the slave select register
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mGetSlaveSelectReg(BaseAddress) \
                    XIo_In32((BaseAddress) + XSP_SSR_OFFSET)

/****************************************************************************/
/**
*
* Enable the device and uninhibit master transactions. Preserves the current
* contents of the control register.
*
* @param    BaseAddress is the  base address of the device
*
* @return   None.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mEnable(BaseAddress) \
{ \
    u16 Control; \
    Control = XSpi_mGetControlReg((BaseAddress)); \
    Control |= XSP_CR_ENABLE_MASK; \
    Control &= ~XSP_CR_TRANS_INHIBIT_MASK; \
    XSpi_mSetControlReg((BaseAddress), Control); \
}

/****************************************************************************/
/**
*
* Disable the device. Preserves the current contents of the control register.
*
* @param    BaseAddress is the  base address of the device
*
* @return   None.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mDisable(BaseAddress) \
             XSpi_mSetControlReg((BaseAddress), \
                     XSpi_mGetControlReg((BaseAddress)) & ~XSP_CR_ENABLE_MASK)


/****************************************************************************/
/**
*
* Send one byte to the currently selected slave. The byte that is received
* from the slave is saved in the receive FIFO/register.
*
* @param    BaseAddress is the  base address of the device
*
* @return   None.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mSendByte(BaseAddress, Data) \
                XIo_Out8((BaseAddress) + XSP_DTR_OFFSET, (Data))


/****************************************************************************/
/**
*
* Receive one byte from the device's receive FIFO/register. It is assumed
* that the byte is already available.
*
* @param    BaseAddress is the  base address of the device
*
* @return   The byte retrieved from the receive FIFO/register.
*
* @note     None.
*
*****************************************************************************/
#define XSpi_mRecvByte(BaseAddress) \
                XIo_In8((BaseAddress) + XSP_DRR_OFFSET)

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

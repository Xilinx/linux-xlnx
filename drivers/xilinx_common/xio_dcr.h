/* $Id: xio_dcr.h,v 1.8 2007/01/24 17:00:16 meinelte Exp $ */
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
* @file xio_dcr.h
*
* The DCR I/O access functions.
*
* @note
*
* These access functions are specific to the PPC440 CPU. Changes might be
* necessary for other members of the IBM PPC Family.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ecm  10/18/05 First release
*                     Need to verify opcodes for mt/mfdcr remain the same.
* 1.00a ecm  11/09/06 Modified from the PPC405 version to use the indirect
*                     addressing that is available in the PPC440 block in V5.
*                     Removed the jump table structure in xio_dcr.c also.
*                     Added functionality from the SEG driver to allow for
*                     one file pair.
* 1.00a ecm  01/02/07 Incorporated changes from testing with multiple DCR
*                     masters, discovered and fixed several concurrency
*                     issues.
* 1.00a ecm  01/24/07 update for new coding standard.
* </pre>
*
* @internal
*
* This code WILL NOT FUNCTION on the PPC405 based architectures, V2P and V4.
*
******************************************************************************/

#ifndef XDCRIO_H        /* prevent circular inclusions */
#define XDCRIO_H        /* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif


/***************************** Include Files *********************************/
#include "xbasic_types.h"

/************************** Constant Definitions *****************************/
/*
 *   256 internal DCR registers
 *   Base address: 2 most signifcant bits of 10-bit addr taken from
 *                 the C_DCRBASEADDR parameter of the processor block.
 *   Offset: 8 least significant bits
 */
/* register base addresses */

#define XDCR_APU_BASE   0x04
#define XDCR_MIB_BASE   0x10
#define XDCR_XB_BASE    0x20
#define XDCR_PLBS0_BASE 0x34
#define XDCR_PLBS1_BASE 0x44
#define XDCR_PLBM_BASE  0x54
#define XDCR_DMA0_BASE  0x80
#define XDCR_DMA1_BASE  0x98
#define XDCR_DMA2_BASE  0xB0
#define XDCR_DMA3_BASE  0xC8

/* register offsets */
/* global registers 0x00-0x02 */

#define XDCR_IDA_ADDR     0x00
#define XDCR_IDA_ACC      0x01
#define XDCR_CTRLCFGSTAT  0x02

/* Auxiliary Processor Unit Controller (APU) 0x04-0x05 */

#define XDCR_APU_UDI  (XDCR_APU_BASE+0x00)
#define XDCR_APU_CTRL (XDCR_APU_BASE+0x01)

/* Memory Interface Bridge (MIB) 0x10-0x13 */

#define XDCR_MIB_CTRL (XDCR_MIB_BASE+0x00)
#define XDCR_MIB_RCON (XDCR_MIB_BASE+0x01)
#define XDCR_MIB_BCON (XDCR_MIB_BASE+0x02)

/* Crossbar (XB) 0x20-0x33 */

#define XDCR_XB_IST      (XDCR_XB_BASE+0x00)
#define XDCR_XB_IMASK    (XDCR_XB_BASE+0x01)
#define XDCR_XB_ARBCFGX  (XDCR_XB_BASE+0x03)
#define XDCR_XB_FIFOSTX  (XDCR_XB_BASE+0x04)
#define XDCR_XB_SMSTX    (XDCR_XB_BASE+0x05)
#define XDCR_XB_MISCX    (XDCR_XB_BASE+0x06)
#define XDCR_XB_ARBCFGM  (XDCR_XB_BASE+0x08)
#define XDCR_XB_FIFOSTM  (XDCR_XB_BASE+0x09)
#define XDCR_XB_SMSTM    (XDCR_XB_BASE+0x0A)
#define XDCR_XB_MISCM    (XDCR_XB_BASE+0x0B)
#define XDCR_XB_TMPL0MAP (XDCR_XB_BASE+0x0D)
#define XDCR_XB_TMPL1MAP (XDCR_XB_BASE+0x0E)
#define XDCR_XB_TMPL2MAP (XDCR_XB_BASE+0x0F)
#define XDCR_XB_TMPL3MAP (XDCR_XB_BASE+0x10)
#define XDCR_XB_TMPLSEL  (XDCR_XB_BASE+0x11)

/* PLB Slave DCR offsets only */

#define XDCR_PLBS_CFG        0x00
#define XDCR_PLBS_SEARU      0x02
#define XDCR_PLBS_SEARL      0x03
#define XDCR_PLBS_SESR       0x04
#define XDCR_PLBS_MISCST     0x05
#define XDCR_PLBS_PLBERRST   0x06
#define XDCR_PLBS_SMST       0x07
#define XDCR_PLBS_MISC       0x08
#define XDCR_PLBS_CMDSNIFF   0x09
#define XDCR_PLBS_CMDSNIFFA  0x0A
#define XDCR_PLBS_TMPL0MAP   0x0C
#define XDCR_PLBS_TMPL1MAP   0x0D
#define XDCR_PLBS_TMPL2MAP   0x0E
#define XDCR_PLBS_TMPL3MAP   0x0F

/* PLB Slave 0 (PLBS0) 0x34-0x43 */

#define XDCR_PLBS0_CFG       (XDCR_PLBS0_BASE+0x00)
#define XDCR_PLBS0_CNT       (XDCR_PLBS0_BASE+0x01)
#define XDCR_PLBS0_SEARU     (XDCR_PLBS0_BASE+0x02)
#define XDCR_PLBS0_SEARL     (XDCR_PLBS0_BASE+0x03)
#define XDCR_PLBS0_SESR      (XDCR_PLBS0_BASE+0x04)
#define XDCR_PLBS0_MISCST    (XDCR_PLBS0_BASE+0x05)
#define XDCR_PLBS0_PLBERRST  (XDCR_PLBS0_BASE+0x06)
#define XDCR_PLBS0_SMST      (XDCR_PLBS0_BASE+0x07)
#define XDCR_PLBS0_MISC      (XDCR_PLBS0_BASE+0x08)
#define XDCR_PLBS0_CMDSNIFF  (XDCR_PLBS0_BASE+0x09)
#define XDCR_PLBS0_CMDSNIFFA (XDCR_PLBS0_BASE+0x0A)
#define XDCR_PLBS0_TMPL0MAP  (XDCR_PLBS0_BASE+0x0C)
#define XDCR_PLBS0_TMPL1MAP  (XDCR_PLBS0_BASE+0x0D)
#define XDCR_PLBS0_TMPL2MAP  (XDCR_PLBS0_BASE+0x0E)
#define XDCR_PLBS0_TMPL3MAP  (XDCR_PLBS0_BASE+0x0F)

/* PLB Slave 1 (PLBS1) 0x44-0x53 */

#define XDCR_PLBS1_CFG       (XDCR_PLBS1_BASE+0x00)
#define XDCR_PLBS1_CNT       (XDCR_PLBS1_BASE+0x01)
#define XDCR_PLBS1_SEARU     (XDCR_PLBS1_BASE+0x02)
#define XDCR_PLBS1_SEARL     (XDCR_PLBS1_BASE+0x03)
#define XDCR_PLBS1_SESR      (XDCR_PLBS1_BASE+0x04)
#define XDCR_PLBS1_MISCST    (XDCR_PLBS1_BASE+0x05)
#define XDCR_PLBS1_PLBERRST  (XDCR_PLBS1_BASE+0x06)
#define XDCR_PLBS1_SMST      (XDCR_PLBS1_BASE+0x07)
#define XDCR_PLBS1_MISC      (XDCR_PLBS1_BASE+0x08)
#define XDCR_PLBS1_CMDSNIFF  (XDCR_PLBS1_BASE+0x09)
#define XDCR_PLBS1_CMDSNIFFA (XDCR_PLBS1_BASE+0x0A)
#define XDCR_PLBS1_TMPL0MAP  (XDCR_PLBS1_BASE+0x0C)
#define XDCR_PLBS1_TMPL1MAP  (XDCR_PLBS1_BASE+0x0D)
#define XDCR_PLBS1_TMPL2MAP  (XDCR_PLBS1_BASE+0x0E)
#define XDCR_PLBS1_TMPL3MAP  (XDCR_PLBS1_BASE+0x0F)

/* PLB Master (PLBM) 0x54-0x5F */

#define XDCR_PLBM_CFG       (XDCR_PLBM_BASE+0x00)
#define XDCR_PLBM_CNT       (XDCR_PLBM_BASE+0x01)
#define XDCR_PLBM_FSEARU    (XDCR_PLBM_BASE+0x02)
#define XDCR_PLBM_FSEARL    (XDCR_PLBM_BASE+0x03)
#define XDCR_PLBM_FSESR     (XDCR_PLBM_BASE+0x04)
#define XDCR_PLBM_MISCST    (XDCR_PLBM_BASE+0x05)
#define XDCR_PLBM_PLBERRST  (XDCR_PLBM_BASE+0x06)
#define XDCR_PLBM_SMST      (XDCR_PLBM_BASE+0x07)
#define XDCR_PLBM_MISC      (XDCR_PLBM_BASE+0x08)
#define XDCR_PLBM_CMDSNIFF  (XDCR_PLBM_BASE+0x09)
#define XDCR_PLBM_CMDSNIFFA (XDCR_PLBM_BASE+0x0A)

/* DMA Controller DCR offsets only */
#define XDCR_DMA_TXNXTDESCPTR   0x00
#define XDCR_DMA_TXCURBUFADDR   0x01
#define XDCR_DMA_TXCURBUFLEN    0x02
#define XDCR_DMA_TXCURDESCPTR   0x03
#define XDCR_DMA_TXTAILDESCPTR  0x04
#define XDCR_DMA_TXCHANNELCTRL  0x05
#define XDCR_DMA_TXIRQ          0x06
#define XDCR_DMA_TXSTATUS       0x07
#define XDCR_DMA_RXNXTDESCPTR   0x08
#define XDCR_DMA_RXCURBUFADDR   0x09
#define XDCR_DMA_RXCURBUFLEN    0x0A
#define XDCR_DMA_RXCURDESCPTR   0x0B
#define XDCR_DMA_RXTAILDESCPTR  0x0C
#define XDCR_DMA_RXCHANNELCTRL  0x0D
#define XDCR_DMA_RXIRQ          0x0E
#define XDCR_DMA_RXSTATUS       0x0F
#define XDCR_DMA_CTRL           0x10

/* DMA Controller 0 (DMA0) 0x80-0x90 */

#define XDCR_DMA0_TXNXTDESCPTR  (XDCR_DMA0_BASE+0x00)
#define XDCR_DMA0_TXCURBUFADDR  (XDCR_DMA0_BASE+0x01)
#define XDCR_DMA0_TXCURBUFLEN   (XDCR_DMA0_BASE+0x02)
#define XDCR_DMA0_TXCURDESCPTR  (XDCR_DMA0_BASE+0x03)
#define XDCR_DMA0_TXTAILDESCPTR (XDCR_DMA0_BASE+0x04)
#define XDCR_DMA0_TXCHANNELCTRL (XDCR_DMA0_BASE+0x05)
#define XDCR_DMA0_TXIRQ         (XDCR_DMA0_BASE+0x06)
#define XDCR_DMA0_TXSTATUS      (XDCR_DMA0_BASE+0x07)
#define XDCR_DMA0_RXNXTDESCPTR  (XDCR_DMA0_BASE+0x08)
#define XDCR_DMA0_RXCURBUFADDR  (XDCR_DMA0_BASE+0x09)
#define XDCR_DMA0_RXCURBUFLEN   (XDCR_DMA0_BASE+0x0A)
#define XDCR_DMA0_RXCURDESCPTR  (XDCR_DMA0_BASE+0x0B)
#define XDCR_DMA0_RXTAILDESCPTR (XDCR_DMA0_BASE+0x0C)
#define XDCR_DMA0_RXCHANNELCTRL (XDCR_DMA0_BASE+0x0D)
#define XDCR_DMA0_RXIRQ         (XDCR_DMA0_BASE+0x0E)
#define XDCR_DMA0_RXSTATUS      (XDCR_DMA0_BASE+0x0F)
#define XDCR_DMA0_CTRL          (XDCR_DMA0_BASE+0x10)

/* DMA Controller 1 (DMA1) 0x98-0xA8 */

#define XDCR_DMA1_TXNXTDESCPTR  (XDCR_DMA1_BASE+0x00)
#define XDCR_DMA1_TXCURBUFADDR  (XDCR_DMA1_BASE+0x01)
#define XDCR_DMA1_TXCURBUFLEN   (XDCR_DMA1_BASE+0x02)
#define XDCR_DMA1_TXCURDESCPTR  (XDCR_DMA1_BASE+0x03)
#define XDCR_DMA1_TXTAILDESCPTR (XDCR_DMA1_BASE+0x04)
#define XDCR_DMA1_TXCHANNELCTRL (XDCR_DMA1_BASE+0x05)
#define XDCR_DMA1_TXIRQ         (XDCR_DMA1_BASE+0x06)
#define XDCR_DMA1_TXSTATUS      (XDCR_DMA1_BASE+0x07)
#define XDCR_DMA1_RXNXTDESCPTR  (XDCR_DMA1_BASE+0x08)
#define XDCR_DMA1_RXCURBUFADDR  (XDCR_DMA1_BASE+0x09)
#define XDCR_DMA1_RXCURBUFLEN   (XDCR_DMA1_BASE+0x0A)
#define XDCR_DMA1_RXCURDESCPTR  (XDCR_DMA1_BASE+0x0B)
#define XDCR_DMA1_RXTAILDESCPTR (XDCR_DMA1_BASE+0x0C)
#define XDCR_DMA1_RXCHANNELCTRL (XDCR_DMA1_BASE+0x0D)
#define XDCR_DMA1_RXIRQ         (XDCR_DMA1_BASE+0x0E)
#define XDCR_DMA1_RXSTATUS      (XDCR_DMA1_BASE+0x0F)
#define XDCR_DMA1_CTRL          (XDCR_DMA1_BASE+0x10)

/* DMA Controller 2 (DMA2) 0xB0-0xC0 */

#define XDCR_DMA2_TXNXTDESCPTR  (XDCR_DMA2_BASE+0x00)
#define XDCR_DMA2_TXCURBUFADDR  (XDCR_DMA2_BASE+0x01)
#define XDCR_DMA2_TXCURBUFLEN   (XDCR_DMA2_BASE+0x02)
#define XDCR_DMA2_TXCURDESCPTR  (XDCR_DMA2_BASE+0x03)
#define XDCR_DMA2_TXTAILDESCPTR (XDCR_DMA2_BASE+0x04)
#define XDCR_DMA2_TXCHANNELCTRL (XDCR_DMA2_BASE+0x05)
#define XDCR_DMA2_TXIRQ         (XDCR_DMA2_BASE+0x06)
#define XDCR_DMA2_TXSTATUS      (XDCR_DMA2_BASE+0x07)
#define XDCR_DMA2_RXNXTDESCPTR  (XDCR_DMA2_BASE+0x08)
#define XDCR_DMA2_RXCURBUFADDR  (XDCR_DMA2_BASE+0x09)
#define XDCR_DMA2_RXCURBUFLEN   (XDCR_DMA2_BASE+0x0A)
#define XDCR_DMA2_RXCURDESCPTR  (XDCR_DMA2_BASE+0x0B)
#define XDCR_DMA2_RXTAILDESCPTR (XDCR_DMA2_BASE+0x0C)
#define XDCR_DMA2_RXCHANNELCTRL (XDCR_DMA2_BASE+0x0D)
#define XDCR_DMA2_RXIRQ         (XDCR_DMA2_BASE+0x0E)
#define XDCR_DMA2_RXSTATUS      (XDCR_DMA2_BASE+0x0F)
#define XDCR_DMA2_CTRL          (XDCR_DMA2_BASE+0x10)

/* DMA Controller 3 (DMA3) 0xC8-0xD8 */

#define XDCR_DMA3_TXNXTDESCPTR  (XDCR_DMA3_BASE+0x00)
#define XDCR_DMA3_TXCURBUFADDR  (XDCR_DMA3_BASE+0x01)
#define XDCR_DMA3_TXCURBUFLEN   (XDCR_DMA3_BASE+0x02)
#define XDCR_DMA3_TXCURDESCPTR  (XDCR_DMA3_BASE+0x03)
#define XDCR_DMA3_TXTAILDESCPTR (XDCR_DMA3_BASE+0x04)
#define XDCR_DMA3_TXCHANNELCTRL (XDCR_DMA3_BASE+0x05)
#define XDCR_DMA3_TXIRQ         (XDCR_DMA3_BASE+0x06)
#define XDCR_DMA3_TXSTATUS      (XDCR_DMA3_BASE+0x07)
#define XDCR_DMA3_RXNXTDESCPTR  (XDCR_DMA3_BASE+0x08)
#define XDCR_DMA3_RXCURBUFADDR  (XDCR_DMA3_BASE+0x09)
#define XDCR_DMA3_RXCURBUFLEN   (XDCR_DMA3_BASE+0x0A)
#define XDCR_DMA3_RXCURDESCPTR  (XDCR_DMA3_BASE+0x0B)
#define XDCR_DMA3_RXTAILDESCPTR (XDCR_DMA3_BASE+0x0C)
#define XDCR_DMA3_RXCHANNELCTRL (XDCR_DMA3_BASE+0x0D)
#define XDCR_DMA3_RXIRQ         (XDCR_DMA3_BASE+0x0E)
#define XDCR_DMA3_RXSTATUS      (XDCR_DMA3_BASE+0x0F)
#define XDCR_DMA3_CTRL          (XDCR_DMA3_BASE+0x10)


/**
 * <pre
 * These are the bit defines for the Control, Configuration, and Status
 * register (XDCR_CTRLCFGSTAT)
 * @{
 */
#define XDCR_INT_MSTR_LOCK_MASK        0x80000000   /* Internal Master Bus Lock */
#define XDCR_INT_MSTR_AUTO_LOCK_MASK   0x40000000   /* Internal Master Bus Auto Lock, RO */
#define XDCR_EXT_MSTR_LOCK_MASK        0x20000000   /* External Master Bus Master Lock */
#define XDCR_EXT_MSTR_AUTO_LOCK_MASK   0x10000000   /* External Master Bus Auto Lock, RO */
#define XDCR_ENB_DCR_AUTO_LOCK_MASK    0x08000000   /* Enable Auto Bus Lock */
#define XDCR_ENB_MSTR_ASYNC_MASK       0x04000000   /* External Master in Async Mode */
#define XDCR_ENB_SLV_ASYNC_MASK        0x02000000   /* External Slave in Async Mode */
#define XDCR_ENB_DCR_TIMEOUT_SUPP_MASK 0x01000000   /* Enable Timeout Support */
#define XDCR_INT_MSTR_TIMEOUT_BIT      0x00000002   /* Internal Master Bus Timeout Occurred */
#define XDCR_EXT_MSTR_TIMEOUT_BIT      0x00000001   /* External Master Bus Timeout Occurred */

/*
 * Mask to disable exceptions in PPC440 MSR
 * Bit 14: Critical Interrupt Enable            0x00020000
 * Bit 16: External Interrupt Enable            0x00008000
 * Bit 20: Floating-point Exceptions Mode 0     0x00000800
 * Bit 23: Floating-point Exceptions Mode 1     0x00000100
 */
#define XDCR_DISABLE_EXCEPTIONS 0xFFFD76FF
#define XDCR_ALL_LOCK           (XDCR_INT_MSTR_LOCK_MASK | XDCR_EXT_MSTR_LOCK_MASK)
#define XDCR_ALL_TIMEOUT        (XDCR_INT_MSTR_TIMEOUT_BIT | XDCR_EXT_MSTR_TIMEOUT_BIT)

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/******************************************************************************/
/**
* Reads the register at the specified DCR address.
*
*
* @param    DcrRegister is the intended source DCR register
*
* @return
*
* Contents of the specified DCR register.
*
* @note
*
* C-style signature:
*    void XIo_mDcrReadReg(u32 DcrRegister)
*
*******************************************************************************/
#define XIo_mDcrReadReg(DcrRegister) ({ mfdcr((DcrRegister)); })

/******************************************************************************/
/**
* Writes the register at specified DCR address.
*
*
* @param    DcrRegister is the intended destination DCR register
* @param    Data is the value to be placed into the specified DRC register
*
* @return
*
* None
*
* @note
*
* C-style signature:
*    void XIo_mDcrWriteReg(u32 DcrRegister, u32 Data)
*
*******************************************************************************/
#define XIo_mDcrWriteReg(DcrRegister, Data) ({ mtdcr((DcrRegister), (Data)); })

/******************************************************************************/
/**
* Explicitly locks the DCR bus
*
* @param    DcrBase is the base of the block of DCR registers
*
* @return
*
* None
*
* @note
*
* C-style signature:
*   void XIo_mDcrLock(u32 DcrBase)
*
*   Sets either Lock bit. Since a master cannot edit another master's Lock bit,
*   the macro can be simplified.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*
*******************************************************************************/
#define XIo_mDcrLock(DcrBase) \
({ \
 mtdcr((DcrBase) | XDCR_CTRLCFGSTAT, \
       (mfdcr((DcrBase) | XDCR_CTRLCFGSTAT) | XDCR_ALL_LOCK) & ~XDCR_ALL_TIMEOUT); \
})

/******************************************************************************/
/**
* Explicitly locks the DCR bus
*
* @param    DcrBase is the base of the block of DCR registers
*
* @return
*
* None
*
* @note
*
* C-style signature:
*   void XIo_mDcrUnlock(u32 DcrBase)
*
*   Unsets either Lock bit. Since a master cannot edit another master's Lock bit,
*   the macro can be simplified.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*
*******************************************************************************/
#define XIo_mDcrUnlock(DcrBase) \
({ \
 mtdcr((DcrBase) | XDCR_CTRLCFGSTAT, \
       (mfdcr((DcrBase) | XDCR_CTRLCFGSTAT) & ~(XDCR_ALL_LOCK | XDCR_ALL_TIMEOUT))); \
})

/******************************************************************************/
/**
* Reads the APU UDI register at the specified APU address.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the intended source APU register
*
* @return
*
* Contents of the specified APU register.
*
* @note
*
* C-style signature:
*    u32 XIo_mDcrReadAPUUDIReg(u32 DcrRegister, u32 UDInum)
*
*   Since reading an APU UDI DCR requires a dummy write to the same DCR,
*   the target UDI number is required. In order to make this operation atomic,
*   interrupts are disabled before and enabled after the DCR accesses.
*   Because an APU UDI access involves two DCR accesses, the DCR bus must be
*   locked to ensure that another master doesn't access the APU UDI register
*   at the same time.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*   Steps:
*   - save old MSR
*   - disable interrupts by writing mask to MSR
*   - acquire lock; since the PPC440 supports timeout wait, it will wait until
*     it successfully acquires the DCR bus lock
*   - shift and mask the UDI number to its bit position of [22:25]
*   - add the DCR base address to the UDI number and perform the read
*   - release DCR bus lock
*   - restore MSR
*   - return value read
*
*******************************************************************************/
#define XIo_mDcrReadAPUUDIReg(DcrBase, UDInum) \
({ \
 unsigned int rVal; \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_DcrLock((DcrBase)); \
 mtdcr((DcrBase) | XDCR_APU_UDI, (((UDInum) << 6) & 0x000003c0) | 0x00000030); \
 rVal = mfdcr((DcrBase) | XDCR_APU_UDI); \
 XIo_DcrUnlock((DcrBase)); \
 mtmsr(oldMSR); \
 rVal; \
})

/******************************************************************************/
/**
* Writes the data to the APU UDI register at the specified APU address.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the intended source APU register
* @param    Data is the value to be placed into the specified APU register
*
* @return
*
* None
*
* @note
*
* C-style signature:
*   void XIo_mDcrWriteAPUUDIReg(u32 DcrRegister, u32 UDInum, u32 Data)
*
*   Since writing an APU UDI DCR requires a dummy write to the same DCR,
*   the target UDI number is required. In order to make this operation atomic,
*   interrupts are disabled before and enabled after the DCR accesses.
*   Because an APU UDI access involves two DCR accesses, the DCR bus must be
*   locked to ensure that another master doesn't access the APU UDI register
*   at the same time.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*   Steps:
*   - save old MSR
*   - disable interrupts by writing mask to MSR
*   - acquire lock, since the PPC440 supports timeout wait, it will wait until
*     it successfully acquires the DCR bus lock
*   - shift and mask the UDI number to its bit position of [22:25]
*   - add DCR base address to UDI number offset and perform the write
*   - release DCR bus lock
*   - restore MSR
*
*******************************************************************************/
#define XIo_mDcrWriteAPUUDIReg(DcrBase, UDInum, Data) \
({ \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_DcrLock((DcrBase)); \
 mtdcr((DcrBase) | XDCR_APU_UDI, (((UDInum) << 6) & 0x000003c0) | 0x00000030); \
 mtdcr((DcrBase) | XDCR_APU_UDI, (Data)); \
 XIo_DcrUnlock((DcrBase)); \
 mtmsr(oldMSR); \
})

/******************************************************************************/
/**
* Reads the register at the specified DCR address using the indirect addressing
* method.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended source DCR register
*
* @return
*
* Contents of the specified DCR register.
*
* @note
*
* C-style signature:
*   void XIo_mDcrIndirectAddrReadReg(u32 DcrBase, u32 DcrRegister)
*
*   Assumes auto-buslocking feature is ON.
*   In order to make this operation atomic, interrupts are disabled before
*   and enabled after the DCR accesses.
*
*******************************************************************************/
#define XIo_mDcrIndirectAddrReadReg(DcrBase, DcrRegister) \
({ \
 unsigned int rVal; \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ADDR, (DcrBase) | DcrRegister); \
 rVal = XIo_mDcrReadReg((DcrBase) | XDCR_IDA_ACC); \
 mtmsr(oldMSR); \
 rVal; \
})

/******************************************************************************/
/**
* Writes the register at specified DCR address using the indirect addressing
* method.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended destination DCR register
* @param    Data is the value to be placed into the specified DRC register
*
* @return
*
* None
*
* @note
*
* C-style signature:
*   void XIo_mDcrIndirectAddrWriteReg(u32 DcrBase, u32 DcrRegister,
*                                  u32 Data)
*
*   Assumes auto-buslocking feature is ON.
*   In order to make this operation atomic, interrupts are disabled before
*   and enabled after the DCR accesses.
*
*******************************************************************************/
#define XIo_mDcrIndirectAddrWriteReg(DcrBase, DcrRegister, Data) \
({ \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ADDR, (DcrBase) | DcrRegister); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ACC, Data); \
 mtmsr(oldMSR); \
})

/******************************************************************************/
/**
* Reads the APU UDI register at the specified DCR address using the indirect
* addressing method.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the intended source DCR register
*
* @return
*
* Contents of the specified APU register.
*
* @note
*
* C-style signature:
*   void XIo_mDcrIndirectAddrReadAPUUDIReg(u32 DcrBase, u32 UDInum)
*
*   An indirect APU UDI read requires three DCR accesses:
*     1) Indirect address reg write
*     2) Indirect access reg write to specify the UDI number
*     3) Indirect access reg read of the actual data
*   Since (2) unlocks the DCR bus, the DCR bus must be explicitly locked
*   instead of relying on the auto-lock feature.
*   In order to make this operation atomic, interrupts are disabled before
*   and enabled after the DCR accesses.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*
*******************************************************************************/
#define XIo_mDcrIndirectAddrReadAPUUDIReg(DcrBase, UDInum) \
({ \
 unsigned int rVal; \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_DcrLock((DcrBase)); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ADDR, (DcrBase) | XDCR_APU_UDI); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ACC, ((UDInum << 6) & 0x000003c0) | 0x00000030); \
 rVal = XIo_mDcrReadReg((DcrBase) | XDCR_IDA_ACC); \
 XIo_DcrUnlock((DcrBase)); \
 mtmsr(oldMSR); \
 rVal; \
})

/******************************************************************************/
/**
* Writes the APU UDI register at specified DCR address using the indirect
* addressing method.
*
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the intended source DCR register
* @param    Data is the value to be placed into the specified DRC register
*
* @return
*
* None
*
* @note
*
* C-style signature:
*   void XIo_mDcrIndirectAddrWriteReg(u32 DcrBase, u32 UDInum, u32 Data)
*
*   An indirect APU UDI write requires three DCR accesses:
*     1) Indirect address reg write
*     2) Indirect access reg write to specify the UDI number
*     3) Indirect access reg write of the actual data
*   Since (2) unlocks the DCR bus, the DCR bus must be explicitly locked
*   instead of relying on the auto-lock feature.
*   In order to make this operation atomic, interrupts are disabled before
*   and enabled after the DCR accesses.
*   Care must be taken to not write a '1' to either timeout bit because
*   it will be cleared.
*
*******************************************************************************/
#define XIo_mDcrIndirectAddrWriteAPUUDIReg(DcrBase, UDInum, Data) \
({ \
 unsigned int oldMSR = mfmsr(); \
 mtmsr(oldMSR & XDCR_DISABLE_EXCEPTIONS); \
 XIo_DcrLock((DcrBase)); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ADDR, (DcrBase) | XDCR_APU_UDI); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ACC, ((UDInum << 6) & 0x000003c0) | 0x00000030); \
 XIo_mDcrWriteReg((DcrBase) | XDCR_IDA_ACC, Data);\
 XIo_DcrUnlock((DcrBase)); \
 mtmsr(oldMSR); \
})

/************************** Function Prototypes ******************************/
void XIo_DcrOut(u32 DcrRegister, u32 Data);
u32 XIo_DcrIn(u32 DcrRegister);

u32 XIo_DcrReadReg(u32 DcrBase, u32 DcrRegister);
void XIo_DcrWriteReg(u32 DcrBase, u32 DcrRegister, u32 Data);
u32 XIo_DcrLockAndReadReg(u32 DcrBase, u32 DcrRegister);
void XIo_DcrLockAndWriteReg(u32 DcrBase, u32 DcrRegister, u32 Data);

void XIo_DcrWriteAPUUDIReg(u32 DcrBase, short UDInum, u32 Data);
u32 XIo_DcrReadAPUUDIReg(u32 DcrBase, short UDInum);

void XIo_DcrLock(u32 DcrBase);
void XIo_DcrUnlock(u32 DcrBase);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

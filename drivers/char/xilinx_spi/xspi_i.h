/* $Id: xspi_i.h,v 1.1 2007/04/04 18:31:38 wre Exp $ */
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
* @file xspi_i.h
*
* This header file contains internal identifiers. It is intended for internal
* use only.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  10/11/01 First release
* 1.00b jhl  03/14/02 Repartitioned driver for smaller files.
* 1.00b rpm  04/24/02 Moved register definitions to xspi_hw.h
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XSPI_I_H		/* prevent circular inclusions */
#define XSPI_I_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xspi_hw.h"

/************************** Constant Definitions *****************************/

/*
 * IPIF SPI device interrupt mask. This mask is for the Device Interrupt
 * Register within the IPIF.
 */
#define XSP_IPIF_SPI_MASK   0x4UL

#define XSP_IPIF_DEVICE_INTR_COUNT  3	/* Number of interrupt sources */
#define XSP_IPIF_IP_INTR_COUNT      6	/* Number of SPI interrupts
					 * note that there are 7 interrupts in
					 * the h/w but s/w does not use the
					 * half empty which only exists when
					 * there are FIFOs, this allows the IPIF
					 * self test to pass with or without
					 * FIFOs */
/*
 * IPIF SPI IP interrupt masks. These masks are for the IP Interrupt Register
 * within the IPIF.
 */
#define XSP_INTR_MODE_FAULT_MASK        0x1UL	/* Mode fault error */
#define XSP_INTR_SLAVE_MODE_FAULT_MASK  0x2UL	/* Selected as slave while
						 * disabled */
#define XSP_INTR_TX_EMPTY_MASK          0x4UL	/* DTR/TxFIFO is empty */
#define XSP_INTR_TX_UNDERRUN_MASK       0x8UL	/* DTR/TxFIFO was underrun */
#define XSP_INTR_RX_FULL_MASK          0x10UL	/* DRR/RxFIFO is full */
#define XSP_INTR_RX_OVERRUN_MASK       0x20UL	/* DRR/RxFIFO was overrun */
#define XSP_INTR_TX_HALF_EMPTY_MASK    0x40UL	/* TxFIFO is half empty */

/*
 * The interrupts we want at startup. We add the TX_EMPTY interrupt in later
 * when we're getting ready to transfer data.  The others we don't care
 * about for now.
 */
#define XSP_INTR_DFT_MASK       (XSP_INTR_MODE_FAULT_MASK |     \
                                 XSP_INTR_TX_UNDERRUN_MASK |    \
                                 XSP_INTR_RX_OVERRUN_MASK |     \
                                 XSP_INTR_SLAVE_MODE_FAULT_MASK)

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/


/*****************************************************************************/
/*
*
* Clear the statistics of the driver instance.
*
* @param    InstancePtr is a pointer to the XSpi instance to be worked on.
*
* @return   None.
*
* @note
*
* Signature: void XSpi_mClearStats(XSpi *InstancePtr)
*
*****************************************************************************/
#define XSpi_mClearStats(InstancePtr) \
{                                           \
    InstancePtr->Stats.ModeFaults = 0;      \
    InstancePtr->Stats.XmitUnderruns = 0;   \
    InstancePtr->Stats.RecvOverruns = 0;    \
    InstancePtr->Stats.SlaveModeFaults = 0; \
    InstancePtr->Stats.BytesTransferred = 0;\
    InstancePtr->Stats.NumInterrupts = 0;   \
}

/************************** Function Prototypes ******************************/

void XSpi_Abort(XSpi * InstancePtr);

/************************** Variable Definitions *****************************/

extern XSpi_Config XSpi_ConfigTable[];

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

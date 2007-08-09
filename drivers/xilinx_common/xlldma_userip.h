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
*       (c) Copyright 2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xlldma_userip.h
*
* This file is for the User-IP core (like Local-Link TEMAC) to define constants
* that are the User-IP core specific. DMA driver requires the constants to work
* correctly. Two constants must be defined in this file:
*
*   - XLLDMA_USR_APPWORD_OFFSET:
*
*     This constant defines a user word the User-IP always updates in the RX
*     Buffer Descriptors (BD) during any Receive transaction.
*
*     The DMA driver initializes this chosen user word of any RX BD to the
*     pre-defined value (see XLLDMA_USR_APPWORD_INITVALUE below) before
*     giving it to the RX channel. The DMA relies on its updation (by the
*     User-IP) to ensure the BD has been completed by the RX channel besides
*     checking the COMPLETE bit in XLLDMA_BD_STSCTRL_USR0_OFFSET field (see
*     xlldma_hw.h).
*
*     The only valid options for this constant are XLLDMA_BD_USR1_OFFSET,
*     XLLDMA_BD_USR2_OFFSET, XLLDMA_BD_USR3_OFFSET and XLLDMA_BD_USR4_OFFSET.
*
*     If the User-IP does not update any of the option fields above, the DMA
*     driver will not work properly.
*
*   - XLLDMA_USR_APPWORD_INITVALUE:
*
*     This constant defines the value the DMA driver uses to populate the
*     XLLDMA_USR_APPWORD_OFFSET field (see above) in any RX BD before giving
*     the BD to the RX channel for receive transaction.
*
*     It must be ensured that the User-IP will always populates a different
*     value from this constant into the XLLDMA_USR_APPWORD_OFFSET field at
*     the end of any receive transaction. Failing to do so will cause the
*     DMA driver to work improperly.
*
* If the User-IP uses different setting, the correct setting must be defined
* in the xparameters.h or as compiler options used in the Makefile. In either
* case the default definition of the constants in this file will be discarded.
*
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a xd   02/21/07 First release
* </pre>
*
******************************************************************************/

#ifndef XLLDMA_USERIP_H		/* prevent circular inclusions */
#define XLLDMA_USERIP_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xlldma_hw.h"
#include "xparameters.h"

/************************** Constant Definitions *****************************/

#ifndef XLLDMA_USERIP_APPWORD_OFFSET
#define XLLDMA_USERIP_APPWORD_OFFSET    XLLDMA_BD_USR4_OFFSET
#endif

#ifndef XLLDMA_USERIP_APPWORD_INITVALUE
#define XLLDMA_USERIP_APPWORD_INITVALUE 0xFFFFFFFF
#endif

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

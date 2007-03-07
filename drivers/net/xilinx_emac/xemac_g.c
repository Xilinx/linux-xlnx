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
* @file xemac_g.c
*
* This file contains a configuration table that specifies the configuration
* of EMAC devices in the system.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  07/31/01 First release
* 1.00b rpm  02/20/02 Repartitioned files and functions
* 1.00c rpm  12/05/02 New version includes support for simple DMA
* 1.00d rpm  09/26/03 New version includes support PLB Ethernet and v2.00a of
*                     the packet fifo driver.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xemac.h"
#include <asm/xparameters.h>

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Prototypes ******************************/

/**
 * This table contains configuration information for each EMAC device
 * in the system.
 */
XEmac_Config XEmac_ConfigTable[] =
{
    {
      XPAR_EMAC_0_DEVICE_ID,       /* Unique ID  of device */
      XPAR_EMAC_0_BASEADDR,        /* Base address of device */
      XPAR_EMAC_0_BASEADDR,        /* Physical Base address of device */
      XPAR_EMAC_0_ERR_COUNT_EXIST, /* Does device have counters? */
      XPAR_EMAC_0_DMA_PRESENT,     /* Does device have scatter-gather DMA? */
      XPAR_EMAC_0_MII_EXIST,       /* Does device support MII? */
      XPAR_EMAC_0_CAM_EXIST,       /* Does device have mutlicast CAM */
      XPAR_EMAC_0_JUMBO_EXIST      /* Does device have jumbo frame capability */
    },
#ifdef XPAR_EMAC_1_BASEADDR
    {
     XPAR_EMAC_1_DEVICE_ID,
     XPAR_EMAC_1_BASEADDR,
     XPAR_EMAC_1_BASEADDR,
     XPAR_EMAC_1_ERR_COUNT_EXIST,
     XPAR_EMAC_1_DMA_PRESENT,
     XPAR_EMAC_1_MII_EXIST,
     XPAR_EMAC_1_CAM_EXIST,
     XPAR_EMAC_1_JUMBO_EXIST
    },
#endif
#ifdef XPAR_EMAC_2_BASEADDR
    {
     XPAR_EMAC_2_DEVICE_ID,
     XPAR_EMAC_2_BASEADDR,
     XPAR_EMAC_2_BASEADDR,
     XPAR_EMAC_2_ERR_COUNT_EXIST,
     XPAR_EMAC_2_DMA_PRESENT,
     XPAR_EMAC_2_MII_EXIST,
     XPAR_EMAC_2_CAM_EXIST,
     XPAR_EMAC_2_JUMBO_EXIST
    },
#endif
};



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

#include <cfg/xparameters.h>
#include "xemac.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Prototypes ******************************/

/**
 * This table contains configuration information for each EMAC device
 * in the system.
 */
XEmac_Config XEmac_ConfigTable[] = {
	{
	 0,			/* Unique ID  of device */
	 CONFIG_XILINX_ETHERNET_0_BASEADDR,	/* Base address of device */
	 CONFIG_XILINX_ETHERNET_0_BASEADDR,	/* Physical Base address of device */
	 CONFIG_XILINX_ETHERNET_0_ERR_COUNT_EXIST,	/* Does device have counters? */
	 CONFIG_XILINX_ETHERNET_0_DMA_PRESENT,	/* Does device have scatter-gather DMA? */
	 CONFIG_XILINX_ETHERNET_0_MII_EXIST,	/* Does device support MII? */
	 CONFIG_XILINX_ETHERNET_0_CAM_EXIST,	/* Does device have mutlicast CAM */
	 CONFIG_XILINX_ETHERNET_0_JUMBO_EXIST	/* Does device have jumbo frame capability */
	 }
	,
#ifdef CONFIG_XILINX_ETHERNET_1_BASEADDR
	{
	 1,
	 CONFIG_XILINX_ETHERNET_1_BASEADDR,
	 CONFIG_XILINX_ETHERNET_1_BASEADDR,
	 CONFIG_XILINX_ETHERNET_1_ERR_COUNT_EXIST,
	 CONFIG_XILINX_ETHERNET_1_DMA_PRESENT,
	 CONFIG_XILINX_ETHERNET_1_MII_EXIST,
	 CONFIG_XILINX_ETHERNET_1_CAM_EXIST,
	 CONFIG_XILINX_ETHERNET_1_JUMBO_EXIST}
	,
#endif
#ifdef CONFIG_XILINX_ETHERNET_2_BASEADDR
	{
	 2,
	 CONFIG_XILINX_ETHERNET_2_BASEADDR,
	 CONFIG_XILINX_ETHERNET_2_BASEADDR,
	 CONFIG_XILINX_ETHERNET_2_ERR_COUNT_EXIST,
	 CONFIG_XILINX_ETHERNET_2_DMA_PRESENT,
	 CONFIG_XILINX_ETHERNET_2_MII_EXIST,
	 CONFIG_XILINX_ETHERNET_2_CAM_EXIST,
	 CONFIG_XILINX_ETHERNET_2_JUMBO_EXIST}
	,
#endif
};

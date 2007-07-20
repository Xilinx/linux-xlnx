/* $Id: xemaclite_g.c,v 1.1.2.1 2007/03/13 17:26:08 akondratenko Exp $ */
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
*       (c) Copyright 2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemaclite_g.c
*
* This file contains a configuration table that specifies the configuration
* of EMACLite devices in the system.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.01a ecm  02/16/04 First release
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xemaclite.h"
#include <linux/autoconf.h>
#include <asm/xparameters.h>

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Prototypes ******************************/

/**
 * This table contains configuration information for each EMACLite device
 * in the system.
 */

XEmacLite_Config XEmacLite_ConfigTable[XPAR_XEMACLITE_NUM_INSTANCES] =
{
    {
	0,				/* Unique ID  of device */
        XPAR_ETHERNETLITE_0_BASEADDR,	    /* Base address */
	0,					    /* Physical address */
        XPAR_ETHERNETLITE_0_TX_PING_PONG,  /* Hardware configuration */
        XPAR_ETHERNETLITE_0_RX_PING_PONG   /* Hardware configuration */
    }
};


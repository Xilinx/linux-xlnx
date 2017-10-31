/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xhdcp1x_port_hdmi.h
* @addtogroup hdcp1x_v4_0
* @{
*
* This file contains the definitions for the hdcp port registers/offsets for
* HDMI interfaces
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 3.1   yas    07/28/16 Added Bitmasks for BSTATUS register.
* </pre>
*
******************************************************************************/

#ifndef XHDCP1X_PORT_HDMI_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP1X_PORT_HDMI_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#if defined(XHDCP1X_PORT_DP_H)
#error "cannot include both xhdcp1x_port_dp.h and xhdcp1x_port_hdmi.h"
#endif

/************************** Constant Definitions *****************************/

/**
 * These constants specify the offsets for the various fields and/or
 * attributes within the hdcp port
 */
#define XHDCP1X_PORT_OFFSET_BKSV	(0x00u)   /**< Bksv Offset        */
#define XHDCP1X_PORT_OFFSET_RO		(0x08u)   /**< Ri'/Ro' Offset     */
#define XHDCP1X_PORT_OFFSET_PJ		(0x0Au)   /**< Pj' Offset         */
#define XHDCP1X_PORT_OFFSET_AKSV	(0x10u)   /**< Aksv Offset        */
#define XHDCP1X_PORT_OFFSET_AINFO	(0x15u)   /**< Ainfo Offset       */
#define XHDCP1X_PORT_OFFSET_AN		(0x18u)   /**< An Offset          */
#define XHDCP1X_PORT_OFFSET_VH0		(0x20u)   /**< V'.H0 Offset       */
#define XHDCP1X_PORT_OFFSET_VH1		(0x24u)   /**< V'.H1 Offset       */
#define XHDCP1X_PORT_OFFSET_VH2		(0x28u)   /**< V'.H2 Offset       */
#define XHDCP1X_PORT_OFFSET_VH3		(0x2Cu)   /**< V'.H3 Offset       */
#define XHDCP1X_PORT_OFFSET_VH4		(0x30u)   /**< V'.H4 Offset       */
#define XHDCP1X_PORT_OFFSET_BCAPS	(0x40u)   /**< Bcaps Offset       */
#define XHDCP1X_PORT_OFFSET_BSTATUS	(0x41u)   /**< Bstatus Offset     */
#define XHDCP1X_PORT_OFFSET_KSVFIFO	(0x43u)   /**< KSV FIFO Offset    */
#define XHDCP1X_PORT_OFFSET_DBG		(0xC0u)   /**< Debug Space Offset */

/**
 * These constants specify the sizes for the various fields and/or
 * attributes within the hdcp port
 */
#define XHDCP1X_PORT_SIZE_BKSV		(0x05u)   /**< Bksv Size          */
#define XHDCP1X_PORT_SIZE_RO		(0x02u)   /**< Ri' Size           */
#define XHDCP1X_PORT_SIZE_PJ		(0x01u)   /**< Pj' Size           */
#define XHDCP1X_PORT_SIZE_AKSV		(0x05u)   /**< Aksv Size          */
#define XHDCP1X_PORT_SIZE_AINFO		(0x01u)   /**< Ainfo Size         */
#define XHDCP1X_PORT_SIZE_AN		(0x08u)   /**< An Size            */
#define XHDCP1X_PORT_SIZE_VH0		(0x04u)   /**< V'.H0 Size         */
#define XHDCP1X_PORT_SIZE_VH1		(0x04u)   /**< V'.H1 Size         */
#define XHDCP1X_PORT_SIZE_VH2		(0x04u)   /**< V'.H2 Size         */
#define XHDCP1X_PORT_SIZE_VH3		(0x04u)   /**< V'.H3 Size         */
#define XHDCP1X_PORT_SIZE_VH4		(0x04u)   /**< V'.H4 Size         */
#define XHDCP1X_PORT_SIZE_BCAPS		(0x01u)   /**< Bcaps Size         */
#define XHDCP1X_PORT_SIZE_BSTATUS	(0x02u)   /**< Bstatus Size       */
#define XHDCP1X_PORT_SIZE_KSVFIFO	(0x01u)   /**< KSV FIFO Size      */
#define XHDCP1X_PORT_SIZE_DBG		(0xC0u)   /**< Debug Space Size   */

/**
 * These constants specify the bit definitions within the various fields
 * and/or attributes within the hdcp port
 */
#define XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE  (1u << 12) /**< BStatus HDMI Mode
							 *  Mask */
#define XHDCP1X_PORT_BIT_BCAPS_FAST_REAUTH  (1u <<  0) /**< BCaps Fast Reauth
							 *  Mask */
#define XHDCP1X_PORT_BIT_BCAPS_1d1_FEATURES (1u <<  1) /**< BCaps HDCP 1.1
							 *  Features Support
							 *  Mask */
#define XHDCP1X_PORT_BIT_BCAPS_FAST	    (1u <<  4) /**< BCaps Fast
							 *  Transfers Mask */
#define XHDCP1X_PORT_BIT_BCAPS_READY	    (1u <<  5) /**< BCaps KSV FIFO
							 *  Ready bit Mask */
#define XHDCP1X_PORT_BIT_BCAPS_REPEATER	    (1u <<  6) /**< BCaps Repeater
							 *  Capable Mask */
#define XHDCP1X_PORT_BIT_BCAPS_HDMI	    (1u <<  7) /**< BCaps HDMI
							 *  Supported Mask */
#define XHDCP1X_PORT_BIT_AINFO_ENABLE_1d1_FEATURES (1u <<  1) /**< AInfo Enable
								*  1.1
								*  Features */

#define XHDCP1X_PORT_BSTATUS_BIT_DEV_CNT_ERR	(1u << 7) /**< BStatus Device
							 *  Count Error Mask */
#define XHDCP1X_PORT_BSTATUS_BIT_DEV_CNT_NO_ERR	(0u << 7) /**< BStatus Device
						  *  Count for No Error Mask */
#define XHDCP1X_PORT_BSTATUS_DEV_CNT_MASK		(0x7F) /**< BStatus
						  *  Device Count Error Mask */
#define XHDCP1X_PORT_BSTATUS_BIT_DEPTH_ERR		(1u << 11) /**< BStatus
							  * Depth Error Mask */
#define XHDCP1X_PORT_BSTATUS_BIT_DEPTH_NO_ERR	(0u << 11) /**< BStatus Depth
						  *  Error for No Error Mask */
#define XHDCP1X_PORT_BSTATUS_DEV_CNT_ERR_SHIFT	(7) /**< BStatus Device
						    *  Count Error Shift Mask*/
#define XHDCP1X_PORT_BSTATUS_DEPTH_ERR_SHIFT	(11) /**< BStatus Depth
							  *  Error Shift Mask*/
#define XHDCP1X_PORT_BSTATUS_DEPTH_SHIFT		(8) /**< BStatus Device
							 *  Count Error Mask */

/**
 * This constant defines the i2c address of the hdcp port
 */
#define XHDCP1X_PORT_PRIMARY_I2C_ADDR	(0x74u)  /**< I2C Addr Primary Link  */
#define XHDCP1X_PORT_SECONDARY_I2C_ADDR	(0x76u)  /**< I2C Addr Secondary Link*/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP1X_PORT_HDMI_H */
/** @} */

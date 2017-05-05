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
* @file xv_hdmirx_hw.h
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDMI RX core.
*
* For more information about the operation of this core see the hardware
* specification and documentation in the higher level driver xv_hdmirx.h file.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  gm, mg 11/03/15 Initial release.
* 1.01  MG     30/12/15 Added DDC peripheral HDCP 2.2 masks
* 1.02  yh     14/01/16 Added Bit Masking for AxisEnable PIO
* 1.03  MG     18/02/16 Added AUX peripheral error event mask
* 1.04  MG     13/05/16 Added DDC HDCP mode mask
* 1.05  MG     27/05/16 Added VTD timebase
* 1.06  MH     26/07/16 Added DDC HDCP protocol event.
* 1.07  YH     25/07/16 Used UINTPTR instead of u32 for BaseAddress
*                       XV_HdmiRx_WriteReg
*                       XV_HdmiRx_ReadReg
* 1.08  YH     14/11/16 Added BRIDGE_YUV420 and BRIDGE_PIXEL mask to PIO Out
* 1.09  MMO    02/03/17 Added XV_HDMIRX_VTD_CTRL_SYNC_LOSS_MASK and
*                          XV_HDMIRX_VTD_STA_SYNC_LOSS_EVT_MASK for HDCP
*                          compliance
* </pre>
*
******************************************************************************/
#ifndef XV_HDMIRX_HW_H_
#define XV_HDMIRX_HW_H_     /**< Prevent circular inclusions
                  *  by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xil_io.h"

/************************** Constant Definitions *****************************/

// VER (Version Interface) peripheral register offsets
#define XV_HDMIRX_VER_BASE                          (0*64)
#define XV_HDMIRX_VER_ID_OFFSET                     ((XV_HDMIRX_VER_BASE)+(0*4))    /**< VER Identification *  Register offset */
#define XV_HDMIRX_VER_VERSION_OFFSET                    ((XV_HDMIRX_VER_BASE)+(1*4))    /**< VER Version Register *  offset */

// PIO (Parallel Interface) peripheral register offsets
#define XV_HDMIRX_PIO_BASE                          (1*64)
#define XV_HDMIRX_PIO_ID_OFFSET                     ((XV_HDMIRX_PIO_BASE)+(0*4))    /**< PIO Identification register offset */
#define XV_HDMIRX_PIO_CTRL_OFFSET                   ((XV_HDMIRX_PIO_BASE)+(1*4))    /**< PIO Control register offset */
#define XV_HDMIRX_PIO_CTRL_SET_OFFSET               ((XV_HDMIRX_PIO_BASE)+(2*4))    /**< PIO Control Register Set offset */
#define XV_HDMIRX_PIO_CTRL_CLR_OFFSET               ((XV_HDMIRX_PIO_BASE)+(3*4))    /**< PIO Control Register Clear offset */
#define XV_HDMIRX_PIO_STA_OFFSET                        ((XV_HDMIRX_PIO_BASE)+(4*4))    /**< PIO Status Register offset */
#define XV_HDMIRX_PIO_OUT_OFFSET                        ((XV_HDMIRX_PIO_BASE)+(5*4))    /**< PIO Out Register offset */
#define XV_HDMIRX_PIO_OUT_SET_OFFSET                    ((XV_HDMIRX_PIO_BASE)+(6*4))    /**< PIO Out Register Set offset */
#define XV_HDMIRX_PIO_OUT_CLR_OFFSET                    ((XV_HDMIRX_PIO_BASE)+(7*4))    /**< PIO Out Register Clear offset */
#define XV_HDMIRX_PIO_OUT_MSK_OFFSET                    ((XV_HDMIRX_PIO_BASE)+(8*4))    /**< PIO Out Mask Register  offset */
#define XV_HDMIRX_PIO_IN_OFFSET                     ((XV_HDMIRX_PIO_BASE)+(9*4))    /**< PIO In Register offset */
#define XV_HDMIRX_PIO_IN_EVT_OFFSET                 ((XV_HDMIRX_PIO_BASE)+(10*4))   /**< PIO In Event Register offset */
#define XV_HDMIRX_PIO_IN_EVT_RE_OFFSET              ((XV_HDMIRX_PIO_BASE)+(11*4))   /**< PIO In Event Rising Edge Register offset */
#define XV_HDMIRX_PIO_IN_EVT_FE_OFFSET              ((XV_HDMIRX_PIO_BASE)+(12*4))   /**< PIO In Event Falling Edge Register offset */

// PIO peripheral Control register masks
#define XV_HDMIRX_PIO_CTRL_RUN_MASK                 (1<<0)  /**< PIO Control Run mask */
#define XV_HDMIRX_PIO_CTRL_IE_MASK                  (1<<1)  /**< PIO Control Interrupt Enable mask */

// PIO peripheral Status register masks
#define XV_HDMIRX_PIO_STA_IRQ_MASK                  (1<<0)  /**< PIO Status Interrupt mask */
#define XV_HDMIRX_PIO_STA_EVT_MASK                  (1<<1)  /**< PIO Status Event mask */

// PIO peripheral PIO Out register masks and shifts
#define XV_HDMIRX_PIO_OUT_RESET_MASK                    (1<<0)  /**< PIO Out Reset mask */
#define XV_HDMIRX_PIO_OUT_LNK_EN_MASK               (1<<1)  /**< PIO Out video enable mask */
#define XV_HDMIRX_PIO_OUT_VID_EN_MASK               (1<<2)  /**< PIO Out video enable mask */
#define XV_HDMIRX_PIO_OUT_HPD_MASK                  (1<<3)  /**< PIO Out Hot-Plug Detect mask */
#define XV_HDMIRX_PIO_OUT_DEEP_COLOR_MASK           0x30    /**< PIO Out Deep Color mask */
#define XV_HDMIRX_PIO_OUT_PIXEL_RATE_MASK           0xC0    /**< PIO Out Pixel Rate mask */
#define XV_HDMIRX_PIO_OUT_SAMPLE_RATE_MASK          0x300   /**< PIO Out Sample Rate mask */
#define XV_HDMIRX_PIO_OUT_COLOR_SPACE_MASK          0xC00   /**< PIO Out Color Space mask */
#define XV_HDMIRX_PIO_OUT_AXIS_EN_MASK              0x80000 /**< PIO Out Axis Enable mask */
#define XV_HDMIRX_PIO_OUT_DEEP_COLOR_SHIFT          4       /**< PIO Out Deep Color shift */
#define XV_HDMIRX_PIO_OUT_PIXEL_RATE_SHIFT          6       /**< PIO Out Pixel Rate Shift */
#define XV_HDMIRX_PIO_OUT_SAMPLE_RATE_SHIFT         8       /**< PIO Out Sample Rate shift */
#define XV_HDMIRX_PIO_OUT_COLOR_SPACE_SHIFT         10      /**< PIO Out Color Space shift */
#define XV_HDMIRX_PIO_OUT_SCRM_MASK                 (1<<12) /**< PIO Out Scrambler mask */
#define XV_HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK        (1<<29) /**< PIO Out Bridge_YUV420 mask */
#define XV_HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK         (1<<30) /**< PIO Out Bridge_Pixel drop mask */

// PIO peripheral PIO In register masks
#define XV_HDMIRX_PIO_IN_DET_MASK                   (1<<0) /**< PIO In cable detect mask */
#define XV_HDMIRX_PIO_IN_LNK_RDY_MASK               (1<<1) /**< PIO In link ready mask */
#define XV_HDMIRX_PIO_IN_VID_RDY_MASK               (1<<2) /**< PIO In video ready mask */
#define XV_HDMIRX_PIO_IN_MODE_MASK                  (1<<3) /**< PIO In Mode mask */
#define XV_HDMIRX_PIO_IN_SCRAMBLER_LOCK0_MASK       (1<<4) /**< PIO In Scrambler lock 0 mask */
#define XV_HDMIRX_PIO_IN_SCRAMBLER_LOCK1_MASK       (1<<5) /**< PIO In Scrambler lock 1 mask */
#define XV_HDMIRX_PIO_IN_SCRAMBLER_LOCK2_MASK       (1<<6) /**< PIO In Scrambler lock 2 mask */
#define XV_HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK (1<<7) /**< PIO In SCDC scrambler enable mask */
#define XV_HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK (1<<8) /**< PIO In SCDC TMDS clock ratio mask */

// Timer peripheral register offsets
#define XV_HDMIRX_TMR_BASE                          (2*64)
#define XV_HDMIRX_TMR_ID_OFFSET                     ((XV_HDMIRX_TMR_BASE)+(0*4))    /**< TMR Identification register offset */
#define XV_HDMIRX_TMR_CTRL_OFFSET                   ((XV_HDMIRX_TMR_BASE)+(1*4))    /**< TMR Control register offset */
#define XV_HDMIRX_TMR_CTRL_SET_OFFSET               ((XV_HDMIRX_TMR_BASE)+(2*4))    /**< TMR Control Register Set offset */
#define XV_HDMIRX_TMR_CTRL_CLR_OFFSET               ((XV_HDMIRX_TMR_BASE)+(3*4))    /**< TMR Control Register Clear offset */
#define XV_HDMIRX_TMR_STA_OFFSET                        ((XV_HDMIRX_TMR_BASE)+(4*4))    /**< TMR Status Register offset */
#define XV_HDMIRX_TMR_CNT_OFFSET                        ((XV_HDMIRX_TMR_BASE)+(5*4))    /**< TMR Counter Register offset */

// Timer peripheral Control register masks
#define XV_HDMIRX_TMR_CTRL_RUN_MASK                 (1<<0)  /**< TMR Control Run mask */
#define XV_HDMIRX_TMR_CTRL_IE_MASK                  (1<<1)  /**< TMR Control Interrupt Enable mask */

// Timer peripheral Status register masks
#define XV_HDMIRX_TMR_STA_IRQ_MASK                  (1<<0)  /**< TMR Status Interrupt mask */
#define XV_HDMIRX_TMR_STA_CNT_EVT_MASK              (1<<1)  /**< TMR Status counter Event mask */

// Video Timing Detector (VTD) peripheral register offsets.
#define XV_HDMIRX_VTD_BASE                          (3*64)
#define XV_HDMIRX_VTD_ID_OFFSET                     ((XV_HDMIRX_VTD_BASE)+(0*4))    /**< VTD Identification Register offset */
#define XV_HDMIRX_VTD_CTRL_OFFSET                   ((XV_HDMIRX_VTD_BASE)+(1*4))    /**< VTD Control Register offset */
#define XV_HDMIRX_VTD_CTRL_SET_OFFSET               ((XV_HDMIRX_VTD_BASE)+(2*4))    /**< VTD Control Set Register offset */
#define XV_HDMIRX_VTD_CTRL_CLR_OFFSET               ((XV_HDMIRX_VTD_BASE)+(3*4))    /**< VTD Control Clear Register offset */
#define XV_HDMIRX_VTD_STA_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(4*4))    /**< VTD Status Register offset */
#define XV_HDMIRX_VTD_TOT_PIX_OFFSET                    ((XV_HDMIRX_VTD_BASE)+(5*4))    /**< VTD Total Pixels Register offset */
#define XV_HDMIRX_VTD_ACT_PIX_OFFSET                    ((XV_HDMIRX_VTD_BASE)+(6*4))    /**< VTD Active Pixels Register offset */
#define XV_HDMIRX_VTD_TOT_LIN_OFFSET                    ((XV_HDMIRX_VTD_BASE)+(7*4))    /**< VTD Total Lines Register offset */
#define XV_HDMIRX_VTD_ACT_LIN_OFFSET                    ((XV_HDMIRX_VTD_BASE)+(8*4))    /**< VTD Active Lines Register offset */
#define XV_HDMIRX_VTD_VSW_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(9*4))    /**< VTD Vertical Sync Width Register offset */
#define XV_HDMIRX_VTD_HSW_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(10*4))   /**< VTD Horizontal Sync Width Register offset */
#define XV_HDMIRX_VTD_VFP_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(11*4))   /**< VTD Vertical Front Porch Register offset */
#define XV_HDMIRX_VTD_VBP_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(12*4))   /**< VTD Vertical Back Porch Register offset */
#define XV_HDMIRX_VTD_HFP_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(13*4))   /**< VTD Horizontal Front Porch Register offset */
#define XV_HDMIRX_VTD_HBP_OFFSET                        ((XV_HDMIRX_VTD_BASE)+(14*4))   /**< VTD Horizontal Back Porch Register offset */

// Video timing detector peripheral Control register masks and shift
#define XV_HDMIRX_VTD_CTRL_RUN_MASK                 (1<<0)  	/**< VTD Control Run mask */
#define XV_HDMIRX_VTD_CTRL_IE_MASK                  (1<<1)  	/**< VTD Control Interrupt Enable mask */
#define XV_HDMIRX_VTD_CTRL_FIELD_POL_MASK           (1<<2)  	/**< VTD Control field polarity mask */
#define XV_HDMIRX_VTD_CTRL_SYNC_LOSS_MASK           (1<<3)    /**< VTD Control field polarity mask */
#define XV_HDMIRX_VTD_CTRL_TIMEBASE_SHIFT          	8      		/**< VTD Control timebase shift */
#define XV_HDMIRX_VTD_CTRL_TIMERBASE_MASK          	0xffffff    /**< VTD Control timebase mask */

// Video timing detector peripheral Status register masks
#define XV_HDMIRX_VTD_STA_IRQ_MASK                  (1<<0)  /**< VTD Status Interrupt mask */
#define XV_HDMIRX_VTD_STA_TIMEBASE_EVT_MASK        	(1<<1)  /**< VTD Status timebase event mask */
#define XV_HDMIRX_VTD_STA_VS_POL_MASK               (1<<3)  /**< VTD Status Vsync Polarity mask */
#define XV_HDMIRX_VTD_STA_HS_POL_MASK               (1<<4)  /**< VTD Status Hsync Polarity mask */
#define XV_HDMIRX_VTD_STA_FMT_MASK                  (1<<5)  /**< VTD Status Format mask */
#define XV_HDMIRX_VTD_STA_SYNC_LOSS_EVT_MASK        (1<<6)  /**< VTD Status Sync Loss mask */

// DDC (Display Data Channel) peripheral register offsets.
#define XV_HDMIRX_DDC_BASE                          (4*64)
#define XV_HDMIRX_DDC_ID_OFFSET                     ((XV_HDMIRX_DDC_BASE)+(0*4))    /**< DDC Identification Register offset */
#define XV_HDMIRX_DDC_CTRL_OFFSET                   ((XV_HDMIRX_DDC_BASE)+(1*4))    /**< DDC Control Register offset */
#define XV_HDMIRX_DDC_CTRL_SET_OFFSET               ((XV_HDMIRX_DDC_BASE)+(2*4))    /**< DDC Control Register Set offset */
#define XV_HDMIRX_DDC_CTRL_CLR_OFFSET               ((XV_HDMIRX_DDC_BASE)+(3*4))    /**< DDC Control Register Clear offset */
#define XV_HDMIRX_DDC_STA_OFFSET                    ((XV_HDMIRX_DDC_BASE)+(4*4))    /**< DDC Status Register offset */
#define XV_HDMIRX_DDC_EDID_STA_OFFSET               ((XV_HDMIRX_DDC_BASE)+(5*4))    /**< DDC EDID Status Register offset */
#define XV_HDMIRX_DDC_HDCP_STA_OFFSET               ((XV_HDMIRX_DDC_BASE)+(6*4))    /**< DDC HDCP Status Register offset */
#define XV_HDMIRX_DDC_EDID_SP_OFFSET                ((XV_HDMIRX_DDC_BASE)+(8*4))    /**< DDC Read EDID segment pointer offset */
#define XV_HDMIRX_DDC_EDID_WP_OFFSET                ((XV_HDMIRX_DDC_BASE)+(9*4))    /**< DDC Read EDID write pointer offset */
#define XV_HDMIRX_DDC_EDID_RP_OFFSET                ((XV_HDMIRX_DDC_BASE)+(10*4))   /**< DDC Read EDID read pointer offset */
#define XV_HDMIRX_DDC_EDID_DATA_OFFSET              ((XV_HDMIRX_DDC_BASE)+(11*4))   /**< DDC Read EDID data offset */
#define XV_HDMIRX_DDC_HDCP_ADDRESS_OFFSET           ((XV_HDMIRX_DDC_BASE)+(12*4))   /**< DDC Read HDCP address offset */
#define XV_HDMIRX_DDC_HDCP_DATA_OFFSET              ((XV_HDMIRX_DDC_BASE)+(13*4))   /**< DDC Read HDCP data offset */

// DDC peripheral Control register masks
#define XV_HDMIRX_DDC_CTRL_RUN_MASK                 (1<<0)  /**< DDC Control Run mask */
#define XV_HDMIRX_DDC_CTRL_IE_MASK                  (1<<1)  /**< DDC Control Interrupt enable mask */
#define XV_HDMIRX_DDC_CTRL_EDID_EN_MASK             (1<<2)  /**< DDC Control EDID enable mask */
#define XV_HDMIRX_DDC_CTRL_SCDC_EN_MASK             (1<<3)  /**< DDC Control SCDC enable mask */
#define XV_HDMIRX_DDC_CTRL_HDCP_EN_MASK             (1<<4)  /**< DDC Control HDCP enable mask */
#define XV_HDMIRX_DDC_CTRL_SCDC_CLR_MASK            (1<<5)  /**< DDC Control SCDC clear mask */
#define XV_HDMIRX_DDC_CTRL_WMSG_CLR_MASK            (1<<6)  /**< DDC Control write message clear mask */
#define XV_HDMIRX_DDC_CTRL_RMSG_CLR_MASK            (1<<7)  /**< DDC Control read message clear mask */
#define XV_HDMIRX_DDC_CTRL_HDCP_MODE_MASK           (1<<8)  /**< DDC Control HDCP mode mask */

// DDC peripheral Status register masks
#define XV_HDMIRX_DDC_STA_IRQ_MASK                  (1<<0)  /**< DDC Status Interrupt mask */
#define XV_HDMIRX_DDC_STA_EVT_MASK                  (1<<1)  /**< DDC Status Event mask */
#define XV_HDMIRX_DDC_STA_BUSY_MASK                 (1<<2)  /**< DDC Status Busy mask */
#define XV_HDMIRX_DDC_STA_SCL_MASK                  (1<<3)  /**< DDC Status state of the SCL input mask */
#define XV_HDMIRX_DDC_STA_SDA_MASK                  (1<<4)  /**< DDC Status state of the SDA input mask */
#define XV_HDMIRX_DDC_STA_HDCP_AKSV_EVT_MASK        (1<<5)  /**< DDC Status HDCP AKSV event mask */
#define XV_HDMIRX_DDC_STA_HDCP_WMSG_NEW_EVT_MASK    (1<<6)  /**< DDC Status HDCP write message buffer new event mask */
#define XV_HDMIRX_DDC_STA_HDCP_RMSG_END_EVT_MASK    (1<<7)  /**< DDC Status HDCP read message buffer end event mask */
#define XV_HDMIRX_DDC_STA_HDCP_RMSG_NC_EVT_MASK     (1<<8)  /**< DDC Status HDCP read message buffer not completed event mask */
#define XV_HDMIRX_DDC_STA_HDCP_1_PROT_MASK          (1<<9)  /**< DDC Status HDCP 1.4 protocol flag */
#define XV_HDMIRX_DDC_STA_HDCP_2_PROT_MASK          (1<<10) /**< DDC Status HDCP 2.2 protocol flag */
#define XV_HDMIRX_DDC_STA_HDCP_1_PROT_EVT_MASK      (1<<11) /**< DDC Status HDCP 1.4 protocol event flag */
#define XV_HDMIRX_DDC_STA_HDCP_2_PROT_EVT_MASK      (1<<12) /**< DDC Status HDCP 2.2 protocol event flag */
#define XV_HDMIRX_DDC_STA_EDID_WORDS_SHIFT          0       /**< DDC Status EDID words shift */
#define XV_HDMIRX_DDC_STA_EDID_WORDS_MASK           0xFFFF  /**< DDC Status EDID words mask */
#define XV_HDMIRX_DDC_STA_HDCP_WMSG_WORDS_MASK      0x7FF   /**< DDC Status HDCP 2.2 write message buffer words mask */
#define XV_HDMIRX_DDC_STA_HDCP_WMSG_WORDS_SHIFT     0       /**< DDC Status HDCP 2.2 write message buffer words shift */
#define XV_HDMIRX_DDC_STA_HDCP_WMSG_EP_MASK         (1<<11) /**< DDC Status HDCP 2.2 write message buffer empty mask */
#define XV_HDMIRX_DDC_STA_HDCP_RMSG_WORDS_MASK      0x7FF   /**< DDC Status HDCP 2.2 read message buffer words mask */
#define XV_HDMIRX_DDC_STA_HDCP_RMSG_WORDS_SHIFT     16      /**< DDC Status HDCP 2.2 read message buffer words shift */
#define XV_HDMIRX_DDC_STA_HDCP_RMSG_EP_MASK         (1<<27) /**< DDC Status HDCP 2.2 read message buffer empty mask */

// Auxiliary (AUX) peripheral register offsets.
#define XV_HDMIRX_AUX_BASE                          (5*64)
#define XV_HDMIRX_AUX_ID_OFFSET                     ((XV_HDMIRX_AUX_BASE)+(0*4))    /**< AUX Identification Register offset */
#define XV_HDMIRX_AUX_CTRL_OFFSET                   ((XV_HDMIRX_AUX_BASE)+(1*4))    /**< AUX Control Register offset */
#define XV_HDMIRX_AUX_CTRL_SET_OFFSET               ((XV_HDMIRX_AUX_BASE)+(2*4))    /**< AUX Control Register Set offset */
#define XV_HDMIRX_AUX_CTRL_CLR_OFFSET               ((XV_HDMIRX_AUX_BASE)+(3*4))    /**< AUX Control Register Clear offset */
#define XV_HDMIRX_AUX_STA_OFFSET                        ((XV_HDMIRX_AUX_BASE)+(4*4))    /**< AUX Status Register offset */
#define XV_HDMIRX_AUX_DAT_OFFSET                        ((XV_HDMIRX_AUX_BASE)+(5*4))    /**< AUX Data Register offset */

// AUX peripheral Control register masks
#define XV_HDMIRX_AUX_CTRL_RUN_MASK                 (1<<0)  /**< AUX Control Run mask */
#define XV_HDMIRX_AUX_CTRL_IE_MASK                  (1<<1)  /**< AUX Control Interrupt Enable mask */

// AUX peripheral Status register masks and shifts
#define XV_HDMIRX_AUX_STA_IRQ_MASK                  (1<<0)  /**< AUX Status Interrupt mask */
#define XV_HDMIRX_AUX_STA_NEW_MASK                  (1<<1)  /**< AUX Status New Packet mask */
#define XV_HDMIRX_AUX_STA_ERR_MASK					(1<<2)	/**< AUX Status New Packet mask */
#define XV_HDMIRX_AUX_STA_AVI_MASK					(1<<3)	/**< AUX Status AVI infoframe mask */
#define XV_HDMIRX_AUX_STA_GCP_MASK					(1<<4)	/**< AUX Status General control packet mask */
#define XV_HDMIRX_AUX_STA_FIFO_EP_MASK              (1<<5)  /**< AUX Status FIFO Empty mask */
#define XV_HDMIRX_AUX_STA_FIFO_FL_MASK              (1<<6)  /**< AUX Status FIFO Full mask */
#define XV_HDMIRX_AUX_STA_GCP_AVMUTE_MASK           (1<<31) /**< AUX Status GCP avmute mask */
#define XV_HDMIRX_AUX_STA_NEW_PKTS_MASK             0x1F    /**< AUX Status New Packets mask */
#define XV_HDMIRX_AUX_STA_AVI_CS_MASK               0x03    /**< AUX Status AVI colorspace mask */
#define XV_HDMIRX_AUX_STA_AVI_VIC_MASK              0x7F    /**< AUX Status AVI VIC mask */
#define XV_HDMIRX_AUX_STA_GCP_CD_MASK               0x03    /**< AUX Status GCP colordepth mask */
#define XV_HDMIRX_AUX_STA_GCP_PP_MASK               0x07    /**< AUX Status GCP pixel phase mask */
#define XV_HDMIRX_AUX_STA_NEW_PKTS_SHIFT                8       /**< AUX Status New Packets Shift */
#define XV_HDMIRX_AUX_STA_AVI_CS_SHIFT              16      /**< AUX Status AVI colorspace Shift */
#define XV_HDMIRX_AUX_STA_AVI_VIC_SHIFT             18      /**< AUX Status AVI VIC Shift */
#define XV_HDMIRX_AUX_STA_GCP_CD_SHIFT              26      /**< AUX Status GCP colordepth Shift */
#define XV_HDMIRX_AUX_STA_GCP_PP_SHIFT              28      /**< AUX Status GCP pixel phase Shift */


// Audio (AUD) peripheral register offsets.
#define XV_HDMIRX_AUD_BASE                          (6*64)
#define XV_HDMIRX_AUD_ID_OFFSET                     ((XV_HDMIRX_AUD_BASE)+(0*4))    /**< AUD Identification Register offset */
#define XV_HDMIRX_AUD_CTRL_OFFSET                   ((XV_HDMIRX_AUD_BASE)+(1*4))    /**< AUD Control Register offset */
#define XV_HDMIRX_AUD_CTRL_SET_OFFSET               ((XV_HDMIRX_AUD_BASE)+(2*4))    /**< AUD Control Register Set offset */
#define XV_HDMIRX_AUD_CTRL_CLR_OFFSET               ((XV_HDMIRX_AUD_BASE)+(3*4))    /**< AUD Control Register Clear offset */
#define XV_HDMIRX_AUD_STA_OFFSET                        ((XV_HDMIRX_AUD_BASE)+(4*4))    /**< AUD Status Register offset */
#define XV_HDMIRX_AUD_CTS_OFFSET                        ((XV_HDMIRX_AUD_BASE)+(5*4))    /**< AUD CTS Register offset */
#define XV_HDMIRX_AUD_N_OFFSET                      ((XV_HDMIRX_AUD_BASE)+(6*4))    /**< AUD N Register offset */

// Audio peripheral Control register masks
#define XV_HDMIRX_AUD_CTRL_RUN_MASK                 (1<<0)  /**< AUD Control Run mask */
#define XV_HDMIRX_AUD_CTRL_IE_MASK                  (1<<1)  /**< AUD Control Interrupt Enable mask */

// AUD peripheral Status register masks and shift
#define XV_HDMIRX_AUD_STA_IRQ_MASK                  (1<<0)  /**< AUD Status Interrupt mask */
#define XV_HDMIRX_AUD_STA_ACT_EVT_MASK              (1<<1)  /**< AUD Status Event mask */
#define XV_HDMIRX_AUD_STA_CH_EVT_MASK               (1<<2)  /**< AUD Status Event mask */
#define XV_HDMIRX_AUD_STA_ACT_MASK                  (1<<3)  /**< AUD Status Active mask */
#define XV_HDMIRX_AUD_STA_AUD_CH_MASK               0x03    /**< AUD Status Audio channel mask */
#define XV_HDMIRX_AUD_STA_AUD_CH_SHIFT              4       /**< AUD Status Audio channel Shift */
#define XV_HDMIRX_AUD_STA_AUD_FMT_MASK              0x07    /**< AUD Status Audio Format mask */
#define XV_HDMIRX_AUD_STA_AUD_FMT_SHIFT             6       /**< AUD Status Audio Format Shift */


// Link Status (LNKSTA) peripheral register offsets.
#define XV_HDMIRX_LNKSTA_BASE                       (7*64)
#define XV_HDMIRX_LNKSTA_ID_OFFSET                  ((XV_HDMIRX_LNKSTA_BASE)+(0*4)) /**< LNKSTA Identification Register offset */
#define XV_HDMIRX_LNKSTA_CTRL_OFFSET                    ((XV_HDMIRX_LNKSTA_BASE)+(1*4)) /**< LNKSTA Control Register offset */
#define XV_HDMIRX_LNKSTA_CTRL_SET_OFFSET                ((XV_HDMIRX_LNKSTA_BASE)+(2*4)) /**< LNKSTA Control Register Set offset */
#define XV_HDMIRX_LNKSTA_CTRL_CLR_OFFSET                ((XV_HDMIRX_LNKSTA_BASE)+(3*4)) /**< LNKSTA Control Register Clear offset */
#define XV_HDMIRX_LNKSTA_STA_OFFSET                 ((XV_HDMIRX_LNKSTA_BASE)+(4*4)) /**< LNKSTA Status Register offset */
#define XV_HDMIRX_LNKSTA_LNK_ERR0_OFFSET                ((XV_HDMIRX_LNKSTA_BASE)+(5*4)) /**< LNKSTA Link Error Counter Channel 0 Register offset */
#define XV_HDMIRX_LNKSTA_LNK_ERR1_OFFSET                ((XV_HDMIRX_LNKSTA_BASE)+(6*4)) /**< LNKSTA Link Error Counter Channel 1 Register offset */
#define XV_HDMIRX_LNKSTA_LNK_ERR2_OFFSET                ((XV_HDMIRX_LNKSTA_BASE)+(7*4)) /**< LNKSTA Link Error Counter Channel 2 Register offset */

// Link Status (LNKSTA) peripheral Control register masks
#define XV_HDMIRX_LNKSTA_CTRL_RUN_MASK              (1<<0)  /**< LNKSTA Control Run mask */
#define XV_HDMIRX_LNKSTA_CTRL_IE_MASK               (1<<1)  /**< LNKSTA Control Interrupt Enable mask */
#define XV_HDMIRX_LNKSTA_CTRL_ERR_CLR_MASK          (1<<2)  /**< LNKSTA Control Error Clear mask */

// Link Status (LNKSTA) peripheral Status register masks
#define XV_HDMIRX_LNKSTA_STA_IRQ_MASK               (1<<0)  /**< LNKSTA Status Interrupt mask */
#define XV_HDMIRX_LNKSTA_STA_ERR_MAX_MASK           (1<<1)  /**< LNKSTA Status Maximum Errors mask */

// Peripheral ID and General shift values.
#define XV_HDMIRX_SHIFT_16      16  /**< 16 shift value */
#define XV_HDMIRX_MASK_16       0xFFFF  /**< 16 bit mask value */
#define XV_HDMIRX_PIO_ID            0x2200  /**< PIO ID */

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/** @name Register access macro definition
* @{
*/
#define XV_HdmiRx_In32  Xil_In32    /**< Input Operations */
#define XV_HdmiRx_Out32 Xil_Out32   /**< Output Operations */

/*****************************************************************************/
/**
*
* This macro reads a value from a HDMI RX register. A 32 bit read is performed.
* If the component is implemented in a smaller width, only the least
* significant data is read from the register. The most significant data
* will be read as 0.
*
* @param    BaseAddress is the base address of the HDMI RX core instance.
* @param    RegOffset is the register offset of the register (defined at
*       the top of this file).
*
* @return   The 32-bit value of the register.
*
* @note     C-style signature:
*       u32 XV_HdmiRx_ReadReg(u32 BaseAddress, u32 RegOffset)
*
******************************************************************************/
#define XV_HdmiRx_ReadReg(BaseAddress, RegOffset) \
    XV_HdmiRx_In32((BaseAddress) + (RegOffset))

/*****************************************************************************/
/**
*
* This macro writes a value to a HDMI RX register. A 32 bit write is performed.
* If the component is implemented in a smaller width, only the least
* significant data is written.
*
* @param    BaseAddress is the base address of the HDMI RX core instance.
* @param    RegOffset is the register offset of the register (defined at
*       the top of this file) to be written.
* @param    Data is the 32-bit value to write into the register.
*
* @return   None.
*
* @note     C-style signature:
*       void XV_HdmiRx_WriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XV_HdmiRx_WriteReg(BaseAddress, RegOffset, Data) \
    XV_HdmiRx_Out32((BaseAddress) + (RegOffset), (u32)(Data))
/*@}*/

/************************** Function Prototypes ******************************/


/************************** Variable Declarations ****************************/


#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

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
* @file xv_hdmitx_hw.h
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDMI TX core.
*
* For more information about the operation of this core see the hardware
* specification and documentation in the higher level driver xv_hdmitx.h file.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.01  YH     25/07/16 Used UINTPTR instead of u32 for BaseAddress
*                       XV_HdmiTx_WriteReg
*                       XV_HdmiTx_ReadReg
* 1.02  YH     14/11/16 Added BRIDGE_YUV420 and BRIDGE_PIXEL mask to PIO Out
* 1.03  MG     06/03/17 Added XV_HDMITX_AUX_STA_PKT_RDY_MASK
* </pre>
*
******************************************************************************/
#ifndef XV_HDMITX_HW_H_
#define XV_HDMITX_HW_H_     /**< Prevent circular inclusions
                  *  by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xil_io.h"

/************************** Constant Definitions *****************************/

/**< VER (Version Interface) peripheral register offsets */
/**< The VER is the first peripheral on the local bus */
#define XV_HDMITX_VER_BASE              (0*64)
#define XV_HDMITX_VER_ID_OFFSET         ((XV_HDMITX_VER_BASE)+(0*4))/**<
                                    * VER Identification *  Register offset */
#define XV_HDMITX_VER_VERSION_OFFSET    ((XV_HDMITX_VER_BASE)+(1*4))/**<
                                    * VER Version Register *  offset */

/**< PIO (Parallel Interface) peripheral register offsets */
/**< The PIO is the first peripheral on the local bus */
#define XV_HDMITX_PIO_BASE              (1*64)
#define XV_HDMITX_PIO_ID_OFFSET         ((XV_HDMITX_PIO_BASE)+(0*4))/**< PIO
                                        * Identification *  Register offset */
#define XV_HDMITX_PIO_CTRL_OFFSET       ((XV_HDMITX_PIO_BASE)+(1*4))/**< PIO
                                        * Control Register *  offset */
#define XV_HDMITX_PIO_CTRL_SET_OFFSET   ((XV_HDMITX_PIO_BASE)+(2*4))/**< PIO
                                        * Control Register Set *  offset */
#define XV_HDMITX_PIO_CTRL_CLR_OFFSET   ((XV_HDMITX_PIO_BASE)+(3*4))/**< PIO
                                        * Control Register Clear *  offset */
#define XV_HDMITX_PIO_STA_OFFSET        ((XV_HDMITX_PIO_BASE)+(4*4))/**< PIO
                                        * Status Register *  offset */
#define XV_HDMITX_PIO_OUT_OFFSET        ((XV_HDMITX_PIO_BASE)+(5*4))/**< PIO
                                        * Out Register offset */
#define XV_HDMITX_PIO_OUT_SET_OFFSET    ((XV_HDMITX_PIO_BASE)+(6*4))/**< PIO
                                        * Out Register Set *  offset */
#define XV_HDMITX_PIO_OUT_CLR_OFFSET    ((XV_HDMITX_PIO_BASE)+(7*4))/**< PIO
                                        * Out Register Clear *  offset */
#define XV_HDMITX_PIO_OUT_MSK_OFFSET    ((XV_HDMITX_PIO_BASE)+(8*4))/**< PIO
                                        * Out Mask Register *  offset */
#define XV_HDMITX_PIO_IN_OFFSET         ((XV_HDMITX_PIO_BASE)+(9*4))/**< PIO
                                        * In Register offset */
#define XV_HDMITX_PIO_IN_EVT_OFFSET     ((XV_HDMITX_PIO_BASE)+(10*4))/**< PIO
                                        * In Event Register *  offset */
#define XV_HDMITX_PIO_IN_EVT_RE_OFFSET  ((XV_HDMITX_PIO_BASE)+(11*4))/**< PIO
                                    * In Event Rising Edge *  Register offset */
#define XV_HDMITX_PIO_IN_EVT_FE_OFFSET  ((XV_HDMITX_PIO_BASE)+(12*4))/**< PIO
                                * In Event Falling Edge *  Register offset */

// PIO peripheral Control register masks
#define XV_HDMITX_PIO_CTRL_RUN_MASK     (1<<0)  /**< PIO Control Run mask */
#define XV_HDMITX_PIO_CTRL_IE_MASK      (1<<1)  /**< PIO Control Interrupt
                                                * Enable mask */

// PIO peripheral Status register masks
#define XV_HDMITX_PIO_STA_IRQ_MASK      (1<<0) /**< PIO Status Interrupt mask */
#define XV_HDMITX_PIO_STA_EVT_MASK      (1<<1) /**< PIO Status Event mask */

// PIO peripheral PIO Out register masks and shifts
#define XV_HDMITX_PIO_OUT_RST_MASK          (1<<0)  /**< PIO Out Reset mask */
#define XV_HDMITX_PIO_OUT_MODE_MASK         (1<<3)  /**< PIO Out Mode mask */
#define XV_HDMITX_PIO_OUT_COLOR_DEPTH_MASK  0x30    /**< PIO Out Color Depth
                                                    * mask */
#define XV_HDMITX_PIO_OUT_PIXEL_RATE_MASK   0xC0    /**< PIO Out Pixel Rate
                                                    * mask */
#define XV_HDMITX_PIO_OUT_SAMPLE_RATE_MASK  0x300   /**< PIO Out Sample Rate
                                                    * mask */
#define XV_HDMITX_PIO_OUT_COLOR_SPACE_MASK  0xC00   /**< PIO Out Color Space
                                                    * mask */
#define XV_HDMITX_PIO_OUT_SCRM_MASK         (1<<12) /**< PIO Out Scrambler
                                                    * mask */
#define XV_HDMITX_PIO_OUT_COLOR_DEPTH_SHIFT 4   /**< PIO Out Color Depth
                                                    * shift */
#define XV_HDMITX_PIO_OUT_PIXEL_RATE_SHIFT  6   /**< PIO Out Pixel Rate
                                                    * shift */
#define XV_HDMITX_PIO_OUT_SAMPLE_RATE_SHIFT 8   /**< PIO Out Sample Rate
                                                    * shift */
#define XV_HDMITX_PIO_OUT_COLOR_SPACE_SHIFT 10  /**< PIO Out Color Space
                                                    * shift */
#define XV_HDMITX_PIO_OUT_BRIDGE_YUV420_MASK (1<<29) /**< PIO Out Bridge_YUV420
                                                         * mask */
#define XV_HDMITX_PIO_OUT_BRIDGE_PIXEL_MASK  (1<<30) /**< PIO Out Bridge_Pixel
                                                         * repeat mask */

// PIO peripheral PIO In register masks
#define XV_HDMITX_PIO_IN_LNK_RDY_MASK       (1<<0)  /**< PIO In link ready
                                                    * mask */
#define XV_HDMITX_PIO_IN_VID_RDY_MASK       (1<<1)  /**< PIO In video ready
                                                    * mask */
#define XV_HDMITX_PIO_IN_HPD_MASK           (1<<2)  /**< PIO In HPD mask */
#define XV_HDMITX_PIO_IN_VS_MASK            (1<<3)  /**< PIO In Vsync mask */
#define XV_HDMITX_PIO_IN_PPP_MASK           0x07    /**< PIO In Pixel packing
                                                    * phase mask */
#define XV_HDMITX_PIO_IN_HPD_TOGGLE_MASK    (1<<8)  /**< PIO In HPD toggle mask */
#define XV_HDMITX_PIO_IN_PPP_SHIFT          5       /**< PIO In Pixel packing
                                                    * phase shift */

/**< DDC (Display Data Channel) peripheral register offsets */
/**< The DDC is the second peripheral on the local bus */
#define XV_HDMITX_DDC_BASE                  (2*64)
#define XV_HDMITX_DDC_ID_OFFSET             ((XV_HDMITX_DDC_BASE)+(0*4))/**< DDC
                                * Identification *  Register offset */
#define XV_HDMITX_DDC_CTRL_OFFSET           ((XV_HDMITX_DDC_BASE)+(1*4))/**< DDC
                                * Control Register *  offset */
#define XV_HDMITX_DDC_CTRL_SET_OFFSET       ((XV_HDMITX_DDC_BASE)+(2*4))/**< DDC
                                * Control Register Set *  offset */
#define XV_HDMITX_DDC_CTRL_CLR_OFFSET       ((XV_HDMITX_DDC_BASE)+(3*4))/**< DDC
                                * Control Register Clear *  offset */
#define XV_HDMITX_DDC_STA_OFFSET            ((XV_HDMITX_DDC_BASE)+(4*4))/**< DDC
                                * Status Register *  offset */
#define XV_HDMITX_DDC_CMD_OFFSET            ((XV_HDMITX_DDC_BASE)+(5*4))/**< DDC
                                * Command Register *  offset */
#define XV_HDMITX_DDC_DAT_OFFSET            ((XV_HDMITX_DDC_BASE)+(6*4))/**< DDC
                                * Data Register *  offset */

// DDC peripheral Control register masks and shift
#define XV_HDMITX_DDC_CTRL_RUN_MASK         (1<<0)  /**< DDC Control Run mask */
#define XV_HDMITX_DDC_CTRL_IE_MASK          (1<<1)  /**< DDC Control Interrupt
                                                    *  Enable mask */
#define XV_HDMITX_DDC_CTRL_CLK_DIV_MASK     0xFFFF  /**< DDC Control Clock
                                                    * Divider mask */
#define XV_HDMITX_DDC_CTRL_CLK_DIV_SHIFT    16  /**< DDC Control Clock
                                                *Divider shift */ /*@}*/

// DDC peripheral Status register masks
#define XV_HDMITX_DDC_STA_IRQ_MASK      (1<<0)  /**< DDC Status IRQ mask */
#define XV_HDMITX_DDC_STA_EVT_MASK      (1<<1)  /**< DDC Status Event mask */
#define XV_HDMITX_DDC_STA_BUSY_MASK     (1<<2)  /**< DDC Status Busy mask */
#define XV_HDMITX_DDC_STA_DONE_MASK     (1<<3)  /**< DDC Status Busy mask */
#define XV_HDMITX_DDC_STA_TIMEOUT_MASK  (1<<4)  /**< DDC Status Timeout mask */
#define XV_HDMITX_DDC_STA_ACK_MASK      (1<<5)  /**< DDC Status ACK mask */
#define XV_HDMITX_DDC_STA_SCL_MASK      (1<<6)  /**< DDC State of SCL Input
                                                * mask */
#define XV_HDMITX_DDC_STA_SDA_MASK      (1<<7)  /**< DDC State of SDA Input
                                                * mask */
#define XV_HDMITX_DDC_STA_CMD_FULL      (1<<8)  /**< Command fifo full */
#define XV_HDMITX_DDC_STA_DAT_EMPTY     (1<<9)  /**< Data fifo empty */
#define XV_HDMITX_DDC_STA_CMD_WRDS_MASK 0xFF /**< Command fifo words mask*/
#define XV_HDMITX_DDC_STA_CMD_WRDS_SHIFT    16  /**< Command fifo words shift */
#define XV_HDMITX_DDC_STA_DAT_WRDS_MASK     0xFF /**< Data fifo words mask */
#define XV_HDMITX_DDC_STA_DAT_WRDS_SHIFT    24  /**< Data fifo words shift */

// DDC peripheral token
#define XV_HDMITX_DDC_CMD_STR_TOKEN     (0x100) /**< Start token */
#define XV_HDMITX_DDC_CMD_STP_TOKEN     (0x101) /**< Stop token */
#define XV_HDMITX_DDC_CMD_RD_TOKEN      (0x102) /**< Read token */
#define XV_HDMITX_DDC_CMD_WR_TOKEN      (0x103) /**< Write token */

// Auxiliary (AUX) peripheral register offsets
// The AUX is the third peripheral on the local bus
#define XV_HDMITX_AUX_BASE              (3*64)
#define XV_HDMITX_AUX_ID_OFFSET         ((XV_HDMITX_AUX_BASE)+(0*4)) /**< AUX
                                * Identification *  Register offset */
#define XV_HDMITX_AUX_CTRL_OFFSET       ((XV_HDMITX_AUX_BASE)+(1*4)) /**< AUX
                                * Control Register *  offset */
#define XV_HDMITX_AUX_CTRL_SET_OFFSET   ((XV_HDMITX_AUX_BASE)+(2*4)) /**< AUX
                                * Control Register Set *  offset */
#define XV_HDMITX_AUX_CTRL_CLR_OFFSET   ((XV_HDMITX_AUX_BASE)+(3*4)) /**< AUX
                                * Control Register Clear *  offset */
#define XV_HDMITX_AUX_STA_OFFSET        ((XV_HDMITX_AUX_BASE)+(4*4)) /**< AUX
                                * Status Register *  offset */
#define XV_HDMITX_AUX_DAT_OFFSET        ((XV_HDMITX_AUX_BASE)+(5*4)) /**< AUX
                                * Data Register *  offset */

// Auxiliary peripheral Control register masks
#define XV_HDMITX_AUX_CTRL_RUN_MASK         (1<<0)  /**< AUX Control Run mask */
#define XV_HDMITX_AUX_CTRL_IE_MASK          (1<<1)  /**< AUX Control Interrupt
                                                    * Enable mask */

// Auxiliary peripheral Status register masks and shift
#define XV_HDMITX_AUX_STA_IRQ_MASK          (1<<0)  /**< AUX Status Interrupt
                                                    *  mask */
#define XV_HDMITX_AUX_STA_FIFO_EMT_MASK     (1<<1)  /**< AUX Status FIFO Empty
                                                    *  mask */
#define XV_HDMITX_AUX_STA_FIFO_FUL_MASK     (1<<2)  /**< AUX Status FIFO Full
                                                    *  mask */
#define XV_HDMITX_AUX_STA_PKT_RDY_MASK     (1<<3)  /**< AUX Status FIFO Ready
                                                    *  mask */
#define XV_HDMITX_AUX_STA_FREE_PKTS_MASK    0x0F    /**< AUX Status Free Packets
                                                    *  mask */
#define XV_HDMITX_AUX_STA_FREE_PKTS_SHIFT   15  /**< AUX Status Free
                                                    *  Packets shift */


// Audio (AUD) peripheral register offsets
// The AUD is the forth peripheral on the local bus
#define XV_HDMITX_AUD_BASE              (4*64)
#define XV_HDMITX_AUD_ID_OFFSET         ((XV_HDMITX_AUD_BASE)+(0*4)) /**< AUD
                                * Identification *  Register offset */
#define XV_HDMITX_AUD_CTRL_OFFSET       ((XV_HDMITX_AUD_BASE)+(1*4)) /**< AUD
                                * Control Register *  offset */
#define XV_HDMITX_AUD_CTRL_SET_OFFSET   ((XV_HDMITX_AUD_BASE)+(2*4)) /**< AUD
                                * Control Register Set *  offset */
#define XV_HDMITX_AUD_CTRL_CLR_OFFSET   ((XV_HDMITX_AUD_BASE)+(3*4)) /**< AUD
                                * Control Register Clear *  offset */
#define XV_HDMITX_AUD_STA_OFFSET        ((XV_HDMITX_AUD_BASE)+(4*4)) /**< AUD
                                * Status Register *  offset */
#define XV_HDMITX_AUD_ACR_CTS_OFFSET    ((XV_HDMITX_AUD_BASE)+(5*4)) /**< AUD
                                * Clock Regeneration CTS *  Register offset */
#define XV_HDMITX_AUD_ACR_N_OFFSET      ((XV_HDMITX_AUD_BASE)+(6*4)) /**< AUD
                                * Clock Regeneration N *  Register offset */

// Audio peripheral Control register masks
#define XV_HDMITX_AUD_CTRL_RUN_MASK     (1<<0)  /**< AUD Control Run mask */
#define XV_HDMITX_AUD_CTRL_IE_MASK      (1<<1)  /**< AUD Control Interrupt
                                                * Enable mask */
#define XV_HDMITX_AUD_CTRL_CH_MASK      0x03 /**< AUD Control channels mask */
#define XV_HDMITX_AUD_CTRL_CH_SHIFT     2   /**< AUD Control channels mask */

// Audio peripheral Status register masks
#define XV_HDMITX_AUD_STA_IRQ_MASK      (1<<0) /**< AUD Status Interrupt mask */

// Peripheral ID and General shift values.
#define XV_HDMITX_SHIFT_16  16  /**< 16 shift value */
#define XV_HDMITX_MASK_16   0xFFFF  /**< 16 bit mask value */
#define XV_HDMITX_PIO_ID    0x2200  /**< TX's PIO ID */

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

// Register access macro definition
#define XV_HdmiTx_In32  Xil_In32    /**< Input Operations */
#define XV_HdmiTx_Out32 Xil_Out32   /**< Output Operations */

/*****************************************************************************/
/**
*
* This macro reads a value from a HDMI TX register. A 32 bit read is performed.
* If the component is implemented in a smaller width, only the least
* significant data is read from the register. The most significant data
* will be read as 0.
*
* @param    BaseAddress is the base address of the HDMI TX core instance.
* @param    RegOffset is the register offset of the register (defined at
*       the top of this file).
*
* @return   The 32-bit value of the register.
*
* @note     C-style signature:
*       u32 XV_HdmiTx_ReadReg(u32 BaseAddress, u32 RegOffset)
*
******************************************************************************/
#define XV_HdmiTx_ReadReg(BaseAddress, RegOffset) \
    XV_HdmiTx_In32((BaseAddress) + (RegOffset))

/*****************************************************************************/
/**
*
* This macro writes a value to a HDMI TX register. A 32 bit write is performed.
* If the component is implemented in a smaller width, only the least
* significant data is written.
*
* @param    BaseAddress is the base address of the HDMI TX core instance.
* @param    RegOffset is the register offset of the register (defined at
*       the top of this file) to be written.
* @param    Data is the 32-bit value to write into the register.
*
* @return   None.
*
* @note     C-style signature:
*       void XV_HdmiTx_WriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XV_HdmiTx_WriteReg(BaseAddress, RegOffset, Data) \
    XV_HdmiTx_Out32((BaseAddress) + (RegOffset), (u32)(Data))
/*@}*/

/************************** Function Prototypes ******************************/


/************************** Variable Declarations ****************************/


#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

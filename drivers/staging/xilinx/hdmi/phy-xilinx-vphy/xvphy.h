/*******************************************************************************
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
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file xvphy.h
 *
 * The Xilinx Video PHY (VPHY) driver. This driver supports the Xilinx Video PHY
 * IP core.
 * Version 1.0 supports:
 * - GTXE2 and GTHE3 GT types.
 * - DisplayPort and HDMI protocols.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   als  10/19/15 Initial release.
 * 1.1   gm   02/01/16 Added EAST, WEST, PLL0 and PLL1 definitions
 *                     for GTPE2.
 *                     Added more events to XVphy_LogEvent definitions
 *                     Added TxBufferBypass in XVphy_Config structure
 *                     Added XVphy_SetDefaultPpc and XVphy_SetPpc functions
 *       als           Added XVphy_GetLineRateHz function.
 * 1.2   gm            Added HdmiFastSwitch in XVphy_Config
 *                     Changed EffectiveAddr datatype in XVphy_CfgInitialize
 *                       to UINTPTR
 *                     Added log events for debugging
 * 1.2   gm   11/11/19 Added TransceiverWidth in XVphy_Config
 * 1.4   gm   29/11/16 Moved internally used APIs to xvphy_i.c/h
 *                     Added preprocessor directives for sw footprint reduction
 *                     Made debug log optional (can be disabled via makefile)
 *                     Added ERR_IRQ type_defs (not for official use for
 *                       Xilinx debugging)
 *                     Added transceiver width, AXIlite clk frequency and
 *                       err_irq in XVphy_Config
 * </pre>
 *
*******************************************************************************/

#ifndef XVPHY_H_
/* Prevent circular inclusions by using protection macros. */
#define XVPHY_H_

#if !defined(XV_CONFIG_LOG_VPHY_DISABLE) && !defined(XV_CONFIG_LOG_DISABLE_ALL)
#define XV_VPHY_LOG_ENABLE
#endif

/******************************* Include Files ********************************/

#include "xil_assert.h"
#include "xvphy_hw.h"
#include "xil_printf.h"
#include "xvidc.h"
//#include "xvphy_dp.h"

/****************************** Type Definitions ******************************/

/* This typedef enumerates the different GT types available. */
typedef enum {
	XVPHY_GT_TYPE_GTXE2 = 1,
	XVPHY_GT_TYPE_GTHE2 = 2,
	XVPHY_GT_TYPE_GTPE2 = 3,
	XVPHY_GT_TYPE_GTHE3 = 4,
	XVPHY_GT_TYPE_GTHE4 = 5,
} XVphy_GtType;

/**
 * This typedef enumerates the various protocols handled by the Video PHY
 * controller (VPHY).
 */
typedef enum {
	XVPHY_PROTOCOL_DP = 0,
	XVPHY_PROTOCOL_HDMI,
	XVPHY_PROTOCOL_NONE
} XVphy_ProtocolType;

/* This typedef enumerates is used to specify RX/TX direction information. */
typedef enum {
	XVPHY_DIR_RX = 0,
	XVPHY_DIR_TX
} XVphy_DirectionType;

/**
 * This typedef enumerates the list of available interrupt handler types. The
 * values are used as parameters to the XVphy_SetIntrHandler function.
 */
typedef enum {
	XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE = XVPHY_INTR_TXRESETDONE_MASK,
	XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE = XVPHY_INTR_RXRESETDONE_MASK,
	XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK = XVPHY_INTR_CPLL_LOCK_MASK,
	XVPHY_INTR_HANDLER_TYPE_QPLL_LOCK = XVPHY_INTR_QPLL_LOCK_MASK,
	XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK = XVPHY_INTR_QPLL_LOCK_MASK,
	XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE = XVPHY_INTR_TXALIGNDONE_MASK,
	XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK = XVPHY_INTR_QPLL1_LOCK_MASK,
	XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE =
		XVPHY_INTR_TXCLKDETFREQCHANGE_MASK,
	XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE =
		XVPHY_INTR_RXCLKDETFREQCHANGE_MASK,
	XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT = XVPHY_INTR_TXTMRTIMEOUT_MASK,
	XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT = XVPHY_INTR_RXTMRTIMEOUT_MASK,
} XVphy_IntrHandlerType;

/**
 * This typedef enumerates the list of available hdmi handler types. The
 * values are used as parameters to the XVphy_SetHdmiCallback function.
 */
typedef enum {
	XVPHY_HDMI_HANDLER_TXINIT = 1,	/**< TX init handler. */
	XVPHY_HDMI_HANDLER_TXREADY,	/**< TX ready handler. */
	XVPHY_HDMI_HANDLER_RXINIT,	/**< RX init handler. */
	XVPHY_HDMI_HANDLER_RXREADY	/**< RX ready handler. */
} XVphy_HdmiHandlerType;

/**
 * This typedef enumerates the different PLL types for a given GT channel.
 */
typedef enum {
	XVPHY_PLL_TYPE_CPLL = 1,
	XVPHY_PLL_TYPE_QPLL = 2,
	XVPHY_PLL_TYPE_QPLL0 = 3,
	XVPHY_PLL_TYPE_QPLL1 = 4,
	XVPHY_PLL_TYPE_PLL0 = 5,
	XVPHY_PLL_TYPE_PLL1 = 6,
	XVPHY_PLL_TYPE_UNKNOWN = 7,
} XVphy_PllType;

/**
 * This typedef enumerates the available channels.
 */
typedef enum {
	XVPHY_CHANNEL_ID_CH1 = 1,
	XVPHY_CHANNEL_ID_CH2 = 2,
	XVPHY_CHANNEL_ID_CH3 = 3,
	XVPHY_CHANNEL_ID_CH4 = 4,
	XVPHY_CHANNEL_ID_CMN0 = 5,
	XVPHY_CHANNEL_ID_CMN1 = 6,
	XVPHY_CHANNEL_ID_CHA = 7,
	XVPHY_CHANNEL_ID_CMNA = 8,
	XVPHY_CHANNEL_ID_CMN = XVPHY_CHANNEL_ID_CMN0,
} XVphy_ChannelId;

/**
 * This typedef enumerates the available reference clocks for the PLL clock
 * selection multiplexer.
 */
typedef enum {
	XVPHY_PLL_REFCLKSEL_TYPE_GTREFCLK0 = XVPHY_REF_CLK_SEL_XPLL_GTREFCLK0,
	XVPHY_PLL_REFCLKSEL_TYPE_GTREFCLK1 = XVPHY_REF_CLK_SEL_XPLL_GTREFCLK1,
	XVPHY_PLL_REFCLKSEL_TYPE_GTNORTHREFCLK0 =
		XVPHY_REF_CLK_SEL_XPLL_GTNORTHREFCLK0,
	XVPHY_PLL_REFCLKSEL_TYPE_GTNORTHREFCLK1 =
		XVPHY_REF_CLK_SEL_XPLL_GTNORTHREFCLK1,
	XVPHY_PLL_REFCLKSEL_TYPE_GTSOUTHREFCLK0 =
		XVPHY_REF_CLK_SEL_XPLL_GTSOUTHREFCLK0,
	XVPHY_PLL_REFCLKSEL_TYPE_GTSOUTHREFCLK1 =
		XVPHY_REF_CLK_SEL_XPLL_GTSOUTHREFCLK1,
	XVPHY_PLL_REFCLKSEL_TYPE_GTEASTREFCLK0 =
		XVPHY_REF_CLK_SEL_XPLL_GTEASTREFCLK0,
	XVPHY_PLL_REFCLKSEL_TYPE_GTEASTREFCLK1 =
		XVPHY_REF_CLK_SEL_XPLL_GTEASTREFCLK1,
	XVPHY_PLL_REFCLKSEL_TYPE_GTWESTREFCLK0 =
		XVPHY_REF_CLK_SEL_XPLL_GTWESTREFCLK0,
	XVPHY_PLL_REFCLKSEL_TYPE_GTWESTREFCLK1 =
		XVPHY_REF_CLK_SEL_XPLL_GTWESTREFCLK1,
	XVPHY_PLL_REFCLKSEL_TYPE_GTGREFCLK =
		XVPHY_REF_CLK_SEL_XPLL_GTGREFCLK,
} XVphy_PllRefClkSelType;

/**
 * This typedef enumerates the available reference clocks used to drive the
 * RX/TX datapaths.
 */
typedef enum {
	XVPHY_SYSCLKSELDATA_TYPE_PLL0_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_PLL0,
	XVPHY_SYSCLKSELDATA_TYPE_PLL1_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_PLL1,
	XVPHY_SYSCLKSELDATA_TYPE_CPLL_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_CPLL,
	XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL,
	XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL0,
	XVPHY_SYSCLKSELDATA_TYPE_QPLL1_OUTCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL1,
} XVphy_SysClkDataSelType;

/**
 * This typedef enumerates the available reference clocks used to drive the
 * RX/TX output clocks.
 */
typedef enum {
	XVPHY_SYSCLKSELOUT_TYPE_CPLL_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CH,
	XVPHY_SYSCLKSELOUT_TYPE_QPLL_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN,
	XVPHY_SYSCLKSELOUT_TYPE_QPLL0_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN0,
	XVPHY_SYSCLKSELOUT_TYPE_QPLL1_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN1,
	XVPHY_SYSCLKSELOUT_TYPE_PLL0_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CH,
	XVPHY_SYSCLKSELOUT_TYPE_PLL1_REFCLK =
		XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN,
} XVphy_SysClkOutSelType;

/**
 * This typedef enumerates the available clocks that are used as multiplexer
 * input selections for the RX/TX output clock.
 */
typedef enum {
	XVPHY_OUTCLKSEL_TYPE_OUTCLKPCS = 1,
	XVPHY_OUTCLKSEL_TYPE_OUTCLKPMA,
	XVPHY_OUTCLKSEL_TYPE_PLLREFCLK_DIV1,
	XVPHY_OUTCLKSEL_TYPE_PLLREFCLK_DIV2,
	XVPHY_OUTCLKSEL_TYPE_PROGDIVCLK
} XVphy_OutClkSelType;

/* This typedef enumerates the possible states a transceiver can be in. */
typedef enum {
	XVPHY_GT_STATE_IDLE,		/**< Idle state. */
	XVPHY_GT_STATE_LOCK,		/**< Lock state. */
	XVPHY_GT_STATE_RESET,		/**< Reset state. */
	XVPHY_GT_STATE_ALIGN,		/**< Align state. */
	XVPHY_GT_STATE_READY,		/**< Ready state. */
} XVphy_GtState;

#ifdef XV_VPHY_LOG_ENABLE
typedef enum {
	XVPHY_LOG_EVT_NONE = 1,		/**< Log event none. */
	XVPHY_LOG_EVT_QPLL_EN,		/**< Log event QPLL enable. */
	XVPHY_LOG_EVT_QPLL_RST,		/**< Log event QPLL reset. */
	XVPHY_LOG_EVT_QPLL_LOCK,	/**< Log event QPLL lock. */
	XVPHY_LOG_EVT_QPLL_RECONFIG,	/**< Log event QPLL reconfig. */
	XVPHY_LOG_EVT_QPLL0_EN,		/**< Log event QPLL0 enable. */
	XVPHY_LOG_EVT_QPLL0_RST,	/**< Log event QPLL0 reset. */
	XVPHY_LOG_EVT_QPLL0_LOCK,	/**< Log event QPLL0 lock. */
	XVPHY_LOG_EVT_QPLL0_RECONFIG,	/**< Log event QPLL0 reconfig. */
	XVPHY_LOG_EVT_QPLL1_EN,		/**< Log event QPLL1 enable. */
	XVPHY_LOG_EVT_QPLL1_RST,	/**< Log event QPLL1 reset. */
	XVPHY_LOG_EVT_QPLL1_LOCK,	/**< Log event QPLL1 lock. */
	XVPHY_LOG_EVT_QPLL1_RECONFIG,	/**< Log event QPLL1 reconfig. */
	XVPHY_LOG_EVT_PLL0_EN,		/**< Log event PLL0 reset. */
	XVPHY_LOG_EVT_PLL0_RST,		/**< Log event PLL0 reset. */
	XVPHY_LOG_EVT_PLL0_LOCK,	/**< Log event PLL0 lock. */
	XVPHY_LOG_EVT_PLL0_RECONFIG,	/**< Log event PLL0 reconfig. */
	XVPHY_LOG_EVT_PLL1_EN,		/**< Log event PLL1 reset. */
	XVPHY_LOG_EVT_PLL1_RST,		/**< Log event PLL1 reset. */
	XVPHY_LOG_EVT_PLL1_LOCK,	/**< Log event PLL1 lock. */
	XVPHY_LOG_EVT_PLL1_RECONFIG,	/**< Log event PLL1 reconfig. */
	XVPHY_LOG_EVT_CPLL_EN,		/**< Log event CPLL reset. */
	XVPHY_LOG_EVT_CPLL_RST,		/**< Log event CPLL reset. */
	XVPHY_LOG_EVT_CPLL_LOCK,	/**< Log event CPLL lock. */
	XVPHY_LOG_EVT_CPLL_RECONFIG,	/**< Log event CPLL reconfig. */
	XVPHY_LOG_EVT_TXPLL_EN,		/**< Log event TXPLL enable. */
	XVPHY_LOG_EVT_TXPLL_RST,	/**< Log event TXPLL reset. */
	XVPHY_LOG_EVT_RXPLL_EN,		/**< Log event RXPLL enable. */
	XVPHY_LOG_EVT_RXPLL_RST,	/**< Log event RXPLL reset. */
	XVPHY_LOG_EVT_GTRX_RST,		/**< Log event GT RX reset. */
	XVPHY_LOG_EVT_GTTX_RST,		/**< Log event GT TX reset. */
	XVPHY_LOG_EVT_VID_TX_RST,	/**< Log event Vid TX reset. */
	XVPHY_LOG_EVT_VID_RX_RST,	/**< Log event Vid RX reset. */
	XVPHY_LOG_EVT_TX_ALIGN,		/**< Log event TX align. */
	XVPHY_LOG_EVT_TX_ALIGN_TMOUT,	/**< Log event TX align Timeout. */
	XVPHY_LOG_EVT_TX_TMR,		/**< Log event TX timer. */
	XVPHY_LOG_EVT_RX_TMR,		/**< Log event RX timer. */
	XVPHY_LOG_EVT_GT_RECONFIG,	/**< Log event GT reconfig. */
	XVPHY_LOG_EVT_GT_TX_RECONFIG,	/**< Log event GT reconfig. */
	XVPHY_LOG_EVT_GT_RX_RECONFIG,	/**< Log event GT reconfig. */
	XVPHY_LOG_EVT_INIT,		/**< Log event init. */
	XVPHY_LOG_EVT_TXPLL_RECONFIG,	/**< Log event TXPLL reconfig. */
	XVPHY_LOG_EVT_RXPLL_RECONFIG,	/**< Log event RXPLL reconfig. */
	XVPHY_LOG_EVT_RXPLL_LOCK,	/**< Log event RXPLL lock. */
	XVPHY_LOG_EVT_TXPLL_LOCK,	/**< Log event TXPLL lock. */
	XVPHY_LOG_EVT_TX_RST_DONE,	/**< Log event TX reset done. */
	XVPHY_LOG_EVT_RX_RST_DONE,	/**< Log event RX reset done. */
	XVPHY_LOG_EVT_TX_FREQ,		/**< Log event TX frequency. */
	XVPHY_LOG_EVT_RX_FREQ,		/**< Log event RX frequency. */
	XVPHY_LOG_EVT_DRU_EN,		/**< Log event DRU enable/disable. */
	XVPHY_LOG_EVT_GT_PLL_LAYOUT,/**< Log event GT PLL Layout Change. */
	XVPHY_LOG_EVT_GT_UNBONDED,  /**< Log event GT Unbonded Change. */
	XVPHY_LOG_EVT_1PPC_ERR,     /**< Log event 1 PPC Error. */
	XVPHY_LOG_EVT_PPC_MSMTCH_ERR,/**< Log event PPC MismatchError. */
	XVPHY_LOG_EVT_VDCLK_HIGH_ERR,/**< Log event VidClk more than 148.5 MHz. */
	XVPHY_LOG_EVT_NO_DRU,		/**< Log event Vid not supported no DRU. */
	XVPHY_LOG_EVT_GT_QPLL_CFG_ERR,/**< Log event QPLL Config not found. */
	XVPHY_LOG_EVT_GT_CPLL_CFG_ERR,/**< Log event QPLL Config not found. */
	XVPHY_LOG_EVT_VD_NOT_SPRTD_ERR,/**< Log event Vid format not supported. */
	XVPHY_LOG_EVT_MMCM_ERR,		/**< Log event MMCM Config not found. */
	XVPHY_LOG_EVT_DUMMY,		/**< Dummy Event should be last */
} XVphy_LogEvent;
#endif

/* This typedef enumerates the possible error conditions. */
typedef enum {
	XVPHY_ERRIRQ_QPLL_CFG    = 0x1,	/**< QPLL CFG not found. */
	XVPHY_ERRIRQ_CPLL_CFG    = 0x2,	/**< CPLL CFG not found. */
	XVPHY_ERRIRQ_NO_DRU      = 0x4,	/**< No DRU in design. */
	XVPHY_ERRIRQ_VD_NOT_SPRTD= 0x8,	/**< Video Not Supported. */
	XVPHY_ERRIRQ_MMCM_CFG    = 0x10,/**< MMCM CFG not found. */
	XVPHY_ERRIRQ_PLL_LAYOUT  = 0x20,/**< PLL Error. */
} XVphy_ErrIrqType;

/******************************************************************************/
/**
 * Callback type which represents the handler for interrupts.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 *
 * @note	None.
 *
*******************************************************************************/
typedef void (*XVphy_IntrHandler)(void *InstancePtr);

/******************************************************************************/
/**
 * Callback type which represents a custom timer wait handler. This is only
 * used for Microblaze since it doesn't have a native sleep function. To avoid
 * dependency on a hardware timer, the default wait functionality is implemented
 * using loop iterations; this isn't too accurate. If a custom timer handler is
 * used, the user may implement their own wait implementation using a hardware
 * timer (see example/) for better accuracy.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 * @param	MicroSeconds is the number of microseconds to be passed to the
 *		timer function.
 *
 * @note	None.
 *
*******************************************************************************/
typedef void (*XVphy_TimerHandler)(void *InstancePtr, u32 MicroSeconds);

/******************************************************************************/
/**
 * Generic callback type.
 *
 * @param	CallbackRef is a pointer to the callback reference.
 *
 * @note	None.
 *
*******************************************************************************/
typedef void (*XVphy_Callback)(void *CallbackRef);

/**
 * This typedef contains configuration information for CPLL/QPLL programming.
 */
typedef struct {
	u8 MRefClkDiv;
	/* Aliases for N (QPLL) and N1/N2 (CPLL). */
	union {
		u8 NFbDivs[2];
		u8 NFbDiv;
		struct {
			u8 N1FbDiv;
			u8 N2FbDiv;
		};
	};
	u16 Cdr[5];
	u8 IsLowerBand;
} XVphy_PllParam;

/**
 * This typedef contains configuration information for PLL type and its
 * reference clock.
 */
typedef struct {
	/* Below members are common between CPLL/QPLL. */
	u64 LineRateHz;				/**< The line rate for the
							channel. */
	union {
		XVphy_PllParam QpllParams;
		XVphy_PllParam CpllParams;	/**< Parameters for a CPLL. */
		XVphy_PllParam PllParams;
	};
	union {
		XVphy_PllRefClkSelType CpllRefClkSel;
						/**< Multiplexer selection for
							the reference clock of
							the CPLL. */
		XVphy_PllRefClkSelType PllRefClkSel;
	};
	/* Below members are CPLL specific. */
	union {
		struct {
			u8 RxOutDiv;		/**< Output clock divider D for
							the RX datapath. */
			u8 TxOutDiv;		/**< Output clock divider D for
							the TX datapath. */
		};
		u8 OutDiv[2];
	};
	union {
		struct {
			XVphy_GtState RxState;	/**< Current state of RX GT. */
			XVphy_GtState TxState;	/**< Current state of TX GT. */
		};
		XVphy_GtState GtState[2];
	};
	union {
		struct {
			XVphy_ProtocolType RxProtocol;
						/**< The protocol which the RX
							path is used for. */
			XVphy_ProtocolType TxProtocol;
						/**< The protocol which the TX
							path is used for. */
		};
		XVphy_ProtocolType Protocol[2];
	};
	union {
		struct {
			XVphy_SysClkDataSelType RxDataRefClkSel;
						/**< Multiplexer selection for
							the reference clock of
							the RX datapath. */
			XVphy_SysClkDataSelType TxDataRefClkSel;
						/**< Multiplexer selection for
							the reference clock of
							the TX datapath. */
		};
		XVphy_SysClkDataSelType DataRefClkSel[2];
	};
	union {
		struct {
			XVphy_SysClkOutSelType RxOutRefClkSel;
						/**< Multiplexer selection for
							the reference clock of
							the RX output clock. */
			XVphy_SysClkOutSelType TxOutRefClkSel;
						/**< Multiplexer selection for
							the reference clock of
							the TX output clock. */
		};
		XVphy_SysClkOutSelType OutRefClkSel[2];
	};
	union {
		struct {
			XVphy_OutClkSelType RxOutClkSel;
						/**< Multiplexer selection for
							which clock to use as
							the RX output clock. */
			XVphy_OutClkSelType TxOutClkSel;
						/**< Multiplexer selection for
							which clock to use as
							the TX output clock. */
		};
		XVphy_OutClkSelType OutClkSel[2];
	};
	union {
		struct {
			u8 RxDelayBypass;	/**< Bypasses the delay
							alignment block for the
							RX output clock. */
			u8 TxDelayBypass;	/**< Bypasses the delay
							alignment block for the
							TX output clock. */
		};
		u8 DelayBypass;
	};
	u8 RxDataWidth;				/**< In bits. */
	u8 RxIntDataWidth;			/**< In bytes. */
	u8 TxDataWidth;				/**< In bits. */
	u8 TxIntDataWidth;			/**< In bytes. */
} XVphy_Channel;

/**
 * This typedef contains configuration information for MMCM programming.
 */
typedef struct {
	u8 DivClkDivide;
	u8 ClkFbOutMult;
	u16 ClkFbOutFrac;
	u8 ClkOut0Div;
	u16 ClkOut0Frac;
	u8 ClkOut1Div;
	u8 ClkOut2Div;
} XVphy_Mmcm;

/**
 * This typedef represents a GT quad.
 */
typedef struct {
	union {
		struct {
			XVphy_Mmcm RxMmcm;	/**< Mixed-mode clock manager
							(MMCM) parameters for
							RX. */
			XVphy_Mmcm TxMmcm;	/**< MMCM parameters for TX. */
		};
		XVphy_Mmcm Mmcm[2];		/**< MMCM parameters. */
	};
	union {
		struct {
			XVphy_Channel Ch1;
			XVphy_Channel Ch2;
			XVphy_Channel Ch3;
			XVphy_Channel Ch4;
			XVphy_Channel Cmn0;
			XVphy_Channel Cmn1;
		};
		XVphy_Channel Plls[6];
	};
	union {
		struct {
			u32 GtRefClk0Hz;
			u32 GtRefClk1Hz;
			u32 GtNorthRefClk0Hz;
			u32 GtNorthRefClk1Hz;
			u32 GtSouthRefClk0Hz;
			u32 GtSouthRefClk1Hz;
			u32 GtgRefClkHz;
		};
		u32 RefClkHz[7];
	};
} XVphy_Quad;

#ifdef XV_VPHY_LOG_ENABLE
/**
 * This typedef contains the logging mechanism for debug.
 */
typedef struct {
	u16 DataBuffer[256];		/**< Log buffer with event data. */
	u8 HeadIndex;			/**< Index of the head entry of the
						Event/DataBuffer. */
	u8 TailIndex;			/**< Index of the tail entry of the
						Event/DataBuffer. */
} XVphy_Log;
#endif

/**
 * This typedef contains configuration information for the Video PHY core.
 */
typedef struct {
	u16 DeviceId;			/**< Device instance ID. */
	UINTPTR BaseAddr;		/**< The base address of the core
						instance. */
	XVphy_GtType XcvrType;		/**< VPHY Transceiver Type */
	u8 TxChannels;			/**< No. of active channels in TX */
	u8 RxChannels;			/**< No. of active channels in RX */
	XVphy_ProtocolType TxProtocol;	/**< Protocol which TX is used for. */
	XVphy_ProtocolType RxProtocol;	/**< Protocol which RX is used for. */
	XVphy_PllRefClkSelType TxRefClkSel; /**< TX REFCLK selection. */
	XVphy_PllRefClkSelType RxRefClkSel; /**< RX REFCLK selection. */
	XVphy_SysClkDataSelType TxSysPllClkSel; /**< TX SYSCLK selection. */
	XVphy_SysClkDataSelType RxSysPllClkSel; /**< RX SYSCLK selectino. */
	u8 DruIsPresent;		/**< A data recovery unit (DRU) exists
						in the design .*/
	XVphy_PllRefClkSelType DruRefClkSel; /**< DRU REFCLK selection. */
	XVidC_PixelsPerClock Ppc;	/**< Number of input pixels per
						 clock. */
	u8 TxBufferBypass;		/**< TX Buffer Bypass is enabled in the
						design. */
	u8  HdmiFastSwitch;		/**< HDMI fast switching is enabled in the
						design. */
	u8  TransceiverWidth;	/**< Transceiver Width seeting in the design */
	u32 ErrIrq;	            /**< Error IRQ is enalbed in design */
	u32 AxiLiteClkFreq;	    /**< AXI Lite Clock Frequency in Hz */
} XVphy_Config;

/* Forward declaration. */
struct XVphy_GtConfigS;

/**
 * The XVphy driver instance data. The user is required to allocate a variable
 * of this type for every XVphy device in the system. A pointer to a variable of
 * this type is then passed to the driver API functions.
 */
typedef struct {
	u32 IsReady;				/**< Device is initialized and
							ready. */
	XVphy_Config Config;			/**< Configuration structure for
							the Video PHY core. */
	const struct XVphy_GtConfigS *GtAdaptor;
#ifdef XV_VPHY_LOG_ENABLE
	XVphy_Log Log;				/**< A log of events. */
#endif
	XVphy_Quad Quads[2];			/**< The quads available to the
							Video PHY core.*/
	u32 HdmiRxRefClkHz;			/**< HDMI RX refclk. */
	u32 HdmiTxRefClkHz;			/**< HDMI TX refclk. */
	u8 HdmiRxTmdsClockRatio;		/**< HDMI TMDS clock ratio. */
	u8 HdmiTxSampleRate;			/**< HDMI TX sample rate. */
	u8 HdmiRxDruIsEnabled;			/**< The DRU is enabled. */
	XVphy_IntrHandler IntrCpllLockHandler;	/**< Callback function for CPLL
							lock interrupts. */
	void *IntrCpllLockCallbackRef;		/**< A pointer to the user data
							passed to the CPLL lock
							callback function. */
	XVphy_IntrHandler IntrQpllLockHandler;	/**< Callback function for QPLL
							lock interrupts. */
	void *IntrQpllLockCallbackRef;		/**< A pointer to the user data
							passed to the QPLL lock
							callback function. */
	XVphy_IntrHandler IntrQpll1LockHandler;	/**< Callback function for QPLL
							lock interrupts. */
	void *IntrQpll1LockCallbackRef;		/**< A pointer to the user data
							passed to the QPLL lock
							callback function. */
	XVphy_IntrHandler IntrTxResetDoneHandler; /**< Callback function for TX
							reset done lock
							interrupts. */
	void *IntrTxResetDoneCallbackRef;	/**< A pointer to the user data
							passed to the TX reset
							done lock callback
							function. */
	XVphy_IntrHandler IntrRxResetDoneHandler; /**< Callback function for RX
							reset done lock
							interrupts. */
	void *IntrRxResetDoneCallbackRef;	/**< A pointer to the user data
							passed to the RX reset
							done lock callback
							function. */
	XVphy_IntrHandler IntrTxAlignDoneHandler; /**< Callback function for TX
							align done lock
							interrupts. */
	void *IntrTxAlignDoneCallbackRef;	/**< A pointer to the user data
							passed to the TX align
							done lock callback
							function. */
	XVphy_IntrHandler IntrTxClkDetFreqChangeHandler; /**< Callback function
							for TX clock detector
							frequency change
							interrupts. */
	void *IntrTxClkDetFreqChangeCallbackRef; /**< A pointer to the user data
							passed to the TX clock
							detector frequency
							change callback
							function. */
	XVphy_IntrHandler IntrRxClkDetFreqChangeHandler; /**< Callback function
							for RX clock detector
							frequency change
							interrupts. */
	void *IntrRxClkDetFreqChangeCallbackRef; /**< A pointer to the user data
							passed to the RX clock
							detector frequency
							change callback
							function. */
	XVphy_IntrHandler IntrTxTmrTimeoutHandler; /**< Callback function for TX
							timer timeout
							interrupts. */
	void *IntrTxTmrTimeoutCallbackRef;	/**< A pointer to the user data
							passed to the TX timer
							timeout callback
							function. */
	XVphy_IntrHandler IntrRxTmrTimeoutHandler; /**< Callback function for RX
							timer timeout
							interrupts. */
	void *IntrRxTmrTimeoutCallbackRef;	/**< A pointer to the user data
							passed to the RX timer
							timeout callback
							function. */
        /* HDMI callbacks. */
	XVphy_Callback HdmiTxInitCallback;	/**< Callback for TX init. */
	void *HdmiTxInitRef;			/**< To be passed to the TX init
							callback. */
	XVphy_Callback HdmiTxReadyCallback;	/**< Callback for TX ready. */
	void *HdmiTxReadyRef;			/**< To be passed to the TX
							ready callback. */
	XVphy_Callback HdmiRxInitCallback;	/**< Callback for RX init. */
	void *HdmiRxInitRef;			/**< To be passed to the RX
							init callback. */
	XVphy_Callback HdmiRxReadyCallback;	/**< Callback for RX ready. */
	void *HdmiRxReadyRef;			/**< To be passed to the RX
							ready callback. */
	XVphy_TimerHandler UserTimerWaitUs;	/**< Custom user function for
							delay/sleep. */
	void *UserTimerPtr;			/**< Pointer to a timer instance
							used by the custom user
							delay/sleep function. */
} XVphy;

/**************************** Function Prototypes *****************************/

/* xvphy.c: Setup and initialization functions. */
void XVphy_CfgInitialize(XVphy *InstancePtr, XVphy_Config *ConfigPtr,
		UINTPTR EffectiveAddr);
u32 XVphy_PllInitialize(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_PllRefClkSelType QpllRefClkSel,
		XVphy_PllRefClkSelType CpllxRefClkSel,
		XVphy_PllType TxPllSelect, XVphy_PllType RxPllSelect);
#if defined (XPAR_XDP_0_DEVICE_ID)
u32 XVphy_ClkInitialize(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir);
#endif
u32 XVphy_GetVersion(XVphy *InstancePtr);
void XVphy_WaitUs(XVphy *InstancePtr, u32 MicroSeconds);
void XVphy_SetUserTimerHandler(XVphy *InstancePtr,
		XVphy_TimerHandler CallbackFunc, void *CallbackRef);

/* xvphy.c: Channel configuration functions - setters. */
u32 XVphy_CfgLineRate(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u64 LineRateHz);
#if defined (XPAR_XDP_0_DEVICE_ID)
u32 XVphy_CfgQuadRefClkFreq(XVphy *InstancePtr, u8 QuadId,
	XVphy_PllRefClkSelType RefClkType, u32 FreqHz);
#endif

/* xvphy.c: Channel configuration functions - getters. */
XVphy_PllType XVphy_GetPllType(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_ChannelId ChId);
u64 XVphy_GetLineRateHz(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);

/* xvphy.c: Reset functions. */
#if defined (XPAR_XDP_0_DEVICE_ID)
u32 XVphy_WaitForPmaResetDone(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir);
u32 XVphy_WaitForResetDone(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir);
u32 XVphy_WaitForPllLock(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);
#endif
u32 XVphy_ResetGtPll(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Hold);
u32 XVphy_ResetGtTxRx(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Hold);

/* xvphy.c: GT/MMCM DRP access. */
u32 XVphy_DrpWrite(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u16 Addr, u16 Val);
u16 XVphy_DrpRead(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u16 Addr);
void XVphy_MmcmPowerDown(XVphy *InstancePtr, u8 QuadId, XVphy_DirectionType Dir,
		u8 Hold);
void XVphy_MmcmStart(XVphy *InstancePtr, u8 QuadId, XVphy_DirectionType Dir);
void XVphy_IBufDsEnable(XVphy *InstancePtr, u8 QuadId, XVphy_DirectionType Dir,
		u8 Enable);
void XVphy_Clkout1OBufTdsEnable(XVphy *InstancePtr, XVphy_DirectionType Dir,
		u8 Enable);
#if defined (XPAR_XDP_0_DEVICE_ID)
void XVphy_BufgGtReset(XVphy *InstancePtr, XVphy_DirectionType Dir, u8 Reset);

/* xvphy.c Miscellaneous control. */
void XVphy_Set8b10b(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Enable);
#endif
u32 XVphy_IsBonded(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);

/* xvphy_log.c: Logging functions. */
void XVphy_LogDisplay(XVphy *InstancePtr);
void XVphy_LogReset(XVphy *InstancePtr);
u16 XVphy_LogRead(XVphy *InstancePtr);
#ifdef XV_VPHY_LOG_ENABLE
void XVphy_LogWrite(XVphy *InstancePtr, XVphy_LogEvent Evt, u8 Data);
#else
#define XVphy_LogWrite(...)
#endif

/* xvphy_intr.c: Interrupt handling functions. */
void XVphy_InterruptHandler(XVphy *InstancePtr);

/* xvphy_selftest.c: Self test function. */
u32 XVphy_SelfTest(XVphy *InstancePtr);

/* xvphy_sinit.c: Configuration extraction function. */
XVphy_Config *XVphy_LookupConfig(u16 DeviceId);

/* xvphy_dp.c, xvphy_hdmi.c, xvphy_hdmi_intr.c: Protocol specific functions. */
u32 XVphy_DpInitialize(XVphy *InstancePtr, XVphy_Config *CfgPtr, u8 QuadId,
		XVphy_PllRefClkSelType CpllRefClkSel,
		XVphy_PllRefClkSelType QpllRefClkSel,
		XVphy_PllType TxPllSelect, XVphy_PllType RxPllSelect,
		u8 LinkRate);
u32 XVphy_HdmiInitialize(XVphy *InstancePtr, u8 QuadId, XVphy_Config *CfgPtr,
		u32 SystemFrequency);
u32 XVphy_SetHdmiTxParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVidC_PixelsPerClock Ppc, XVidC_ColorDepth Bpc,
		XVidC_ColorFormat ColorFormat);
u32 XVphy_SetHdmiRxParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);

u32 XVphy_HdmiCfgCalcMmcmParam(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir,
		XVidC_PixelsPerClock Ppc, XVidC_ColorDepth Bpc);

void XVphy_HdmiUpdateClockSelection(XVphy *InstancePtr, u8 QuadId,
		XVphy_SysClkDataSelType TxSysPllClkSel,
		XVphy_SysClkDataSelType RxSysPllClkSel);
void XVphy_ClkDetFreqReset(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir);
u32 XVphy_ClkDetGetRefClkFreqHz(XVphy *InstancePtr, XVphy_DirectionType Dir);
u32 XVphy_DruGetRefClkFreqHz(XVphy *InstancePtr);
void XVphy_HdmiDebugInfo(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);
void XVphy_DpDebugInfo(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);
void XVphy_SetHdmiCallback(XVphy *InstancePtr,
		XVphy_HdmiHandlerType HandlerType,
		void *CallbackFunc, void *CallbackRef);

/******************* Macros (Inline Functions) Definitions ********************/

#define XVPHY_CH2IDX(Id)	((Id) - XVPHY_CHANNEL_ID_CH1)
#define XVPHY_ISCH(Id)		(((Id) == XVPHY_CHANNEL_ID_CHA) || \
	((XVPHY_CHANNEL_ID_CH1 <= (Id)) && ((Id) <= XVPHY_CHANNEL_ID_CH4)))
#define XVPHY_ISCMN(Id)		(((Id) == XVPHY_CHANNEL_ID_CMNA) || \
	((XVPHY_CHANNEL_ID_CMN0 <= (Id)) && ((Id) <= XVPHY_CHANNEL_ID_CMN1)))

#define XVphy_IsTxUsingQpll(InstancePtr, QuadId, ChId) \
        ((XVPHY_PLL_TYPE_QPLL == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId)) || \
        (XVPHY_PLL_TYPE_QPLL0 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId)) || \
        (XVPHY_PLL_TYPE_QPLL1 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId)) || \
	(XVPHY_PLL_TYPE_PLL0 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId)) || \
	(XVPHY_PLL_TYPE_PLL1 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId)))
#define XVphy_IsRxUsingQpll(InstancePtr, QuadId, ChId) \
        ((XVPHY_PLL_TYPE_QPLL == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId)) || \
        (XVPHY_PLL_TYPE_QPLL0 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId)) || \
        (XVPHY_PLL_TYPE_QPLL1 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId)) || \
	(XVPHY_PLL_TYPE_PLL0 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId)) || \
	(XVPHY_PLL_TYPE_PLL1 == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId)))
#define XVphy_IsTxUsingCpll(InstancePtr, QuadId, ChId) \
        (XVPHY_PLL_TYPE_CPLL == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_TX, ChId))
#define XVphy_IsRxUsingCpll(InstancePtr, QuadId, ChId) \
        (XVPHY_PLL_TYPE_CPLL == \
		XVphy_GetPllType(InstancePtr, QuadId, XVPHY_DIR_RX, ChId))

#define XVPHY_GTXE2 1
#define XVPHY_GTHE2 2
#define XVPHY_GTPE2 3
#define XVPHY_GTHE3 4
#define XVPHY_GTHE4 5

#endif /* XVPHY_H_ */

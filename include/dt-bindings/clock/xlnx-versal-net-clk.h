/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022, Xilinx Inc.
 * Copyright (C) 2022, Advanced Micro Devices, Inc.
 */

#ifndef _DT_BINDINGS_CLK_VERSAL_NET_H
#define _DT_BINDINGS_CLK_VERSAL_NET_H

#include <dt-bindings/clock/xlnx-versal-clk.h>

#define GEM0_REF_RX	0xA9
#define GEM0_REF_TX	0xA8
#define GEM1_REF_RX	0xA2
#define GEM1_REF_TX	0xA1
#define CAN0_REF_2X	0x9E
#define CAN1_REF_2X	0xAC
#define FPD_WWDT	0x96
#define ACPU_0		0x98
#define ACPU_1		0x9B
#define ACPU_2		0x9A
#define ACPU_3		0x99
#define I3C0_REF	0x9D
#define I3C1_REF	0x9F
#define USB1_BUS_REF	0xAE

/* Remove Versal specific node IDs */
#undef APU_PLL
#undef RPU_PLL
#undef CPM_PLL
#undef APU_PRESRC
#undef APU_POSTCLK
#undef APU_PLL_OUT
#undef APLL
#undef RPU_PRESRC
#undef RPU_POSTCLK
#undef RPU_PLL_OUT
#undef RPLL
#undef CPM_PRESRC
#undef CPM_POSTCLK
#undef CPM_PLL_OUT
#undef CPLL
#undef APLL_TO_XPD
#undef RPLL_TO_XPD
#undef RCLK_PMC
#undef RCLK_LPD
#undef WDT
#undef MUXED_IRO_DIV2
#undef MUXED_IRO_DIV4
#undef PSM_REF
#undef CPM_CORE_REF
#undef CPM_LSBUS_REF
#undef CPM_DBG_REF
#undef CPM_AUX0_REF
#undef CPM_AUX1_REF
#undef CPU_R5
#undef CPU_R5_CORE
#undef CPU_R5_OCM
#undef CPU_R5_OCM2
#undef CAN0_REF
#undef CAN1_REF
#undef I2C0_REF
#undef I2C1_REF
#undef CPM_TOPSW_REF
#undef USB3_DUAL_REF
#undef MUXED_IRO
#undef PL_EXT
#undef PL_LB
#undef MIO_50_OR_51
#undef MIO_24_OR_25

#endif

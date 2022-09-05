/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2022 Xilinx, Inc.
 */

#ifndef _DT_BINDINGS_VERSAL_NET_POWER_H
#define _DT_BINDINGS_VERSAL_NET_POWER_H

#include <dt-bindings/power/xlnx-versal-power.h>

#define PM_DEV_USB_1				(0x182240D7U)

/* Remove Versal specific node IDs */
#undef PM_DEV_RPU0_0
#undef PM_DEV_RPU0_1
#undef PM_DEV_OCM_0
#undef PM_DEV_OCM_1
#undef PM_DEV_OCM_2
#undef PM_DEV_OCM_3
#undef PM_DEV_TCM_0_A
#undef PM_DEV_TCM_1_A
#undef PM_DEV_TCM_0_B
#undef PM_DEV_TCM_1_B
#undef PM_DEV_SWDT_FPD
#undef PM_DEV_ADMA_0
#undef PM_DEV_ADMA_1
#undef PM_DEV_ADMA_2
#undef PM_DEV_ADMA_3
#undef PM_DEV_ADMA_4
#undef PM_DEV_ADMA_5
#undef PM_DEV_ADMA_6
#undef PM_DEV_ADMA_7
#undef PM_DEV_AI

#endif

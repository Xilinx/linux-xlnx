/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __SOC_ZYNQMP_FIRMWARE_DEBUG_H__
#define __SOC_ZYNQMP_FIRMWARE_DEBUG_H__

#include <linux/soc/xilinx/zynqmp/firmware.h>

int zynqmp_pm_self_suspend(const u32 node,
			   const u32 latency,
			   const u32 state);
int zynqmp_pm_abort_suspend(const enum zynqmp_pm_abort_reason reason);
int zynqmp_pm_register_notifier(const u32 node, const u32 event,
				const u32 wake, const u32 enable);
#endif /* __SOC_ZYNQMP_FIRMWARE_DEBUG_H__ */

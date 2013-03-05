/*
 * This file contains common function prototypes to avoid externs
 * in the c files.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_ZYNQ_COMMON_H__
#define __MACH_ZYNQ_COMMON_H__

#include <mach/slcr.h>

void __init xttcps_timer_init_old(void);
void platform_device_init(void);

int __cpuinit zynq_cpun_start(u32 address, int cpu);

static inline void xilinx_system_reset(char mode, const char *cmd)
{
	xslcr_system_reset();
}

/* multiplatform use core.h for this purpose */
extern void secondary_startup(void);

extern void __iomem *zynq_slcr_base;
extern void __iomem *scu_base;

#ifdef CONFIG_SUSPEND
int zynq_pm_late_init(void);
#else
static int zynq_pm_late_init(void)
{
	return 0;
}
#endif

extern unsigned int zynq_sys_suspend_sz;
int zynq_sys_suspend(void __iomem *ddrc_base, void __iomem *slcr_base);

#endif

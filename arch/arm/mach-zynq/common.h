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

void zynq_secondary_startup(void);

extern int zynq_slcr_init(void);
extern int zynq_early_slcr_init(void);
extern void zynq_slcr_system_reset(void);
extern void zynq_slcr_cpu_stop(int cpu);
extern void zynq_slcr_cpu_start(int cpu);
extern bool zynq_slcr_cpu_state_read(int cpu);
extern void zynq_slcr_cpu_state_write(int cpu, bool die);
extern u32 zynq_slcr_get_ocm_config(void);
extern u32 zynq_slcr_get_device_id(void);

#ifdef CONFIG_SMP
extern char zynq_secondary_trampoline;
extern char zynq_secondary_trampoline_jump;
extern char zynq_secondary_trampoline_end;
extern int zynq_cpun_start(u32 address, int cpu);
extern struct smp_operations zynq_smp_ops __initdata;
#endif

extern void zynq_slcr_init_preload_fpga(void);
extern void zynq_slcr_init_postload_fpga(void);

extern void __iomem *zynq_slcr_base;
extern void __iomem *zynq_scu_base;

void zynq_pm_late_init(void);
extern unsigned int zynq_sys_suspend_sz;
int zynq_sys_suspend(void __iomem *ddrc_base, void __iomem *slcr_base);

static inline void zynq_prefetch_init(void)
{
	/*
	 * Enable prefetching in aux control register. L2 prefetch must
	 * only be enabled if the slave supports it (PL310 does)
	 */
	asm volatile ("mrc   p15, 0, r1, c1, c0, 1\n"
#ifdef CONFIG_XILINX_PREFETCH
		      "orr   r1, r1, #6\n"
#else
		      "bic   r1, r1, #6\n"
#endif
		      "mcr   p15, 0, r1, c1, c0, 1\n"
		      : : : "r1");
}

static inline void zynq_core_pm_init(void)
{
	/* A9 clock gating */
	asm volatile ("mrc  p15, 0, r12, c15, c0, 0\n"
		      "orr  r12, r12, #1\n"
		      "mcr  p15, 0, r12, c15, c0, 0\n"
		      : /* no outputs */
		      : /* no inputs */
		      : "r12");
}

#endif

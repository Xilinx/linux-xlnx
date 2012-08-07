/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_IRQ_H
#define _ASM_MICROBLAZE_IRQ_H

/*
 * Linux IRQ# is currently offset by one to map to the hardware
 * irq number. So hardware IRQ0 maps to Linux irq 1.
 */
#define NO_IRQ_OFFSET	1
#define IRQ_OFFSET	NO_IRQ_OFFSET
/* AXI PCIe MSI support */
#if defined(CONFIG_XILINX_AXIPCIE) && defined(CONFIG_PCI_MSI)
#define IRQ_XILINX_MSI_0	128
#define XILINX_NUM_MSI_IRQS	32
#define NR_IRQS		(32 + IRQ_XILINX_MSI_0 + IRQ_OFFSET)
#else
#define NR_IRQS		(32 + IRQ_OFFSET)
#endif

#include <asm-generic/irq.h>

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

/* should be defined in each interrupt controller driver */
extern unsigned int get_irq(void);

#endif /* _ASM_MICROBLAZE_IRQ_H */

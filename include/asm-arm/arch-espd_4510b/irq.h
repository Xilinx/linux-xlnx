/*
 *  linux/include/asm-armnommu/arch-espd_4510b/irq.h
 *
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *                         Hyok S. Choi (hyok.choi@samsung.com)
 *
 */
#ifndef __ARCH_S3C4510B_irq_h
#define __ARCH_S3C4510B_irq_h

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>

extern unsigned  int fixup_irq(int i);

extern void do_IRQ(int irq, struct pt_regs *regs);

extern void s3c4510b_init_irq(void);

#define 	irq_init_irq 	s3c4510b_init_irq

#endif 

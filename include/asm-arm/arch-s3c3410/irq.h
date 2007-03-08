/*
 *  linux/include/asm-armnommu/arch-s3c3410/irq.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *                         Hyok S. Choi (hyok.choi@samsung.com)
 *
 */
#ifndef __S3C3410_irq_h
#define __S3C3410_irq_h

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>

extern unsigned  int fixup_irq(int i);

extern void do_IRQ(int irq, struct pt_regs *regs);

extern void s3c3410_init_irq(void);

#define 	irq_init_irq 	s3c3410_init_irq

#endif 

/*
 * arch/arm/arch-espd_4510b/dma.c
 *
 * Copyright (C) 2003 SAMSUNG ELECTRONICS Co., Ltd. 
 *	      Hyok S. Choi (hyok.choi@samsung.com)
 *     
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/mach/dma.h>

void arch_dma_init(dma_t *dma)
{
}

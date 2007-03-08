/*
 *  linux/include/asm-armnommu/arch-espd_4510b/system.h
 *
 *  Copyright (C) 2002 SAMSUNG ELECTRONICS
 *                       Hyok S. Choi <hyok.choi@samsung.com>
 *
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/hardware.h>
#include <asm/io.h>


extern void __do_dump( const char *s);
static void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
 	printk(KERN_ERR"arch_reset() not implemented\n");
 	BUG();
}

#endif

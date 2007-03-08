/*
 * include/asm-arm/arch-s3c24a0/system.h
 * 
 * $Id: system.h,v 1.3 2006/12/12 13:13:07 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/arch/hardware.h>

static inline 
void arch_idle(void)
{
	/* TODO */
	cpu_do_idle(/*0*/);
}

static inline 
void arch_reset(char mode)
{
	if (mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		WTCNT = 0x100;
		WTDAT = 0x100;
		WTCON = 0x8021;
	}
}

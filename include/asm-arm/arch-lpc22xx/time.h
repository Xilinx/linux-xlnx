/*
 *  linux/include/asm-arm/arch-lpc22xx/time.h
 *
 *  Copyright (C) 2004 Philips Semiconductors
 */

#ifndef __ASM_ARCH_TIME_H__
#define __ASM_ARCH_TIME_H__

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/timex.h>

#define CLOCKS_PER_USEC	(LPC22xx_Fpclk/1000000)

/* set timer to generate interrupt every 10ms */
/* MR0/(LPC22xx_Fpclk/(PR0+1)) = 10/1000 = 0.01s */
#define MR0_INIT_VALUE	10				
#define PRESCALE_COUNTER_INIT_VALUE (LPC22xx_Fpclk/1000) - 1 


#endif /*__ASM_ARCH_TIME_H__*/

/*
 * linux/include/asm-arm/arch-atmel/time.h
 *
 * Copyright (C) 2001/02 Erwin Authried <eauth@softsys.co.at>
 * Modified by Hyok S. Choi for 2.6, 2004.
 */

#ifndef __ASM_ARCH_TIME_H__
#define __ASM_ARCH_TIME_H__

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/timex.h>

#define CLOCKS_PER_USEC	(CONFIG_ARM_CLK/1000000)

#if (KERNEL_TIMER==0)
#   define KERNEL_TIMER_IRQ_NUM IRQ_TC0
#elif (KERNEL_TIMER==1)
#   define KERNEL_TIMER_IRQ_NUM IRQ_TC1
#elif (KERNEL_TIMER==2)
#   define KERNEL_TIMER_IRQ_NUM IRQ_TC2
#else
#error Wierd -- KERNEL_TIMER is not defined or something....
#endif

#endif


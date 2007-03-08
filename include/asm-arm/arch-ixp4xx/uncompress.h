/*
 * include/asm-arm/arch-ixp4xx/uncompress.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ARCH_UNCOMPRESS_H_
#define _ARCH_UNCOMPRESS_H_

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <linux/serial_reg.h>

#define TX_DONE (UART_LSR_TEMT|UART_LSR_THRE)

static int console_output = 1;
static volatile u32* uart_base;

static inline void putc(int c)
{
	/* Check THRE and TEMT bits before we transmit the character.
	 */
	if (console_output) {
		while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE)
			barrier();

		*uart_base = c;
	}
}

static void flush(void)
{
}

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
	/*
	 * Coyote and gtwx5715 only have UART2 connected
	 */
	if (machine_is_adi_coyote() || machine_is_gtwx5715())
		uart_base = (volatile u32*) IXP4XX_UART2_BASE_PHYS;
	else
		uart_base = (volatile u32*) IXP4XX_UART1_BASE_PHYS;

	if (machine_is_ess710() || machine_is_ivpn() || machine_is_sg560() ||
	    machine_is_sg565() || machine_is_sg580() || machine_is_sg720() ||
	    machine_is_shiva1100() || machine_is_sg590())
		console_output = 0;
}

/*
 * arch_id is a variable in decompress_kernel()
 */
#define arch_decomp_setup()	__arch_decomp_setup(arch_id)

#if defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580) || \
    defined(CONFIG_MACH_ESS710) || defined(CONFIG_MACH_SG720) || \
    defined(CONFIG_MACH_SG590) || defined(CONFIG_MACH_IVPN)
#define arch_decomp_wdog() \
	 *((volatile u32 *)(IXP4XX_GPIO_BASE_PHYS+IXP4XX_GPIO_GPOUTR_OFFSET)) ^= 0x00004000
#elif defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SHIVA1100)
#define arch_decomp_wdog() \
	*((volatile unsigned char *) SG565_WATCHDOG_BASE_PHYS) = 0
#else
#define arch_decomp_wdog()
#endif

#endif

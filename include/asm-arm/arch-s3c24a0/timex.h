/*
 * include/asm-arm/arch-s3c24a0/timex.h
 *
 * $Id: timex.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on linux/include/asm-arm/arch-s3c2410/timex.h
 */


/* If a value of TCFG1 is a, a value of divider is 2 << a */
#define CLK_DIVIDER             2
/* a value of TCFG0_PRE1 */
#define CLK_PRESCALE            15

#include <asm/arch/clocks.h>

/* PCLK */
// #define CLK_INPUT               elfin_get_bus_clk(GET_PCLK)
#define CLK_INPUT               51000000 /* 204-102-51 MHz */

/*#define CLOCK_TICK_RATE               1562500 */
#define CLOCK_TICK_RATE         (CLK_INPUT / (CLK_PRESCALE ) / CLK_DIVIDER)
#define CLOCK_TICK_FACTOR       80

/*
 *  arch/arm/mach-s3c24a0/clocks.c
 *
 *  $Id: clocks.c,v 1.3 2006/12/12 13:38:48 gerg Exp $
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/errno.h>
#include <asm/arch/clocks.h>



static unsigned long get_usb_clk_freq(int who)
{
        unsigned long val = UPLLCON;

        if (CLKSRC & (1<<7)) return 0;  /* UPLL OFF */
        val = ((GET_MDIV(val) + 8) * FIN)/((GET_PDIV(val) + 2) * (1 << GET_SDIV(val)));
        return val;
}

/* 
 * CLKDIVN  differs from S3C24A0X to S3C24A0A 
 * --SW.LEE
 */

static inline unsigned long
cal_bus_clk(unsigned long cpu_clk, unsigned long ratio, int who)
{
	unsigned long hclk = 0;
	unsigned long pclk = 0;

	if (who == GET_UPLL)
		return get_usb_clk_freq(GET_UPLL); 

	switch (ratio & 0x6) {
		case 0:
			hclk = cpu_clk;
			break;
		case 2:
			hclk = cpu_clk/2;
			break; 
		case 4:
			hclk = cpu_clk/4;
			break; 
		default:
			panic("Wrong Value in CLKDIVN");
	}
	switch (ratio & 0x1) {
		case 0:
			pclk = hclk;
			break;
		case 1:
			pclk = hclk/2;
			break;
	}

	if (who == GET_HCLK) 
			return hclk;
	else {
		if (who == GET_PCLK) 
			return pclk;
		else 
			panic("Wrong Clock requested ");
	}
}


/*
 * cpu clock = (((mdiv + 8) * FIN) / ((pdiv + 2) * (1 << sdiv)))
 *  FIN = Input Frequency (to CPU)
 */
unsigned long
elfin_get_cpu_clk(void)
{
	unsigned long val = MPLLCON;
	
	return (((GET_MDIV(val) + 8) * FIN) / ((GET_PDIV(val) + 2) * (1 << GET_SDIV(val))));
}
EXPORT_SYMBOL(elfin_get_cpu_clk);

unsigned long
elfin_get_bus_clk(int who)
{
	unsigned long cpu_clk = elfin_get_cpu_clk();
	unsigned long ratio = CLKDIVN_BUS;

	return (cal_bus_clk(cpu_clk, ratio, who));
}
EXPORT_SYMBOL(elfin_get_bus_clk);

#define MEGA	(1000 * 1000)
static int __init elfin_cpu_init(void)
{
	unsigned long freq, hclk, pclk;

	freq = elfin_get_cpu_clk();
	hclk = elfin_get_bus_clk(GET_HCLK);
	pclk = elfin_get_bus_clk(GET_PCLK);

	printk(KERN_INFO "CPU clock = %ld.%03ld Mhz,", freq / MEGA, freq % MEGA);
	
	printk(" HCLK = %ld.%03ld Mhz, PCLK = %ld.%03ld Mhz\n",
		 hclk / MEGA, hclk % MEGA, pclk / MEGA, pclk % MEGA);

	return 0;
}

__initcall(elfin_cpu_init);

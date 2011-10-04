/*
 *  derived from linux/arch/arm/mach-versatile/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2011 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/clkdev.h>

/* The primary purpose for these platform devices is to support frame 
   buffer in QEMU as they are not supported in hardware.  Once device
   tree is supported by these drivers this won't be needed. 
*/

/* The addresses and interrupts used by these 2 devices, for the keyboard
   and mouse are not supported in h/w and could overlap with something
   a customer creates in the FPGA fabric.
*/

#define KMI0_IRQ	{ 60, NO_IRQ }
#define KMI1_IRQ	{ 61, NO_IRQ }

/*
 * These are fixed clocks.
 */
static struct clk ref_clk = {
	.rate	= 50000000,
};

static struct clk_lookup lookups[] = {
	{	/* KMI0 */
		.dev_id		= "ps2-keyboard",
		.clk		= &ref_clk,
	}, {	/* KMI1 */
		.dev_id		= "ps2-mouse",
		.clk		= &ref_clk,
	},
};

static struct amba_device kmi0_device = {			
	.dev		= {					
		.coherent_dma_mask = ~0,			
		.init_name = "ps2-keyboard",				
	},							
	.res		= {					
		.start	= 0xE0112000,		
		.end	= 0xE0112000 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,			
	},							
	.dma_mask	= ~0,					
	.irq		= KMI0_IRQ,				
};

static struct amba_device kmi1_device = {			
	.dev		= {					
		.coherent_dma_mask = ~0,			
		.init_name = "ps2-mouse",				
	},							
	.res		= {					
		.start	= 0xE0113000,		
		.end	= 0xE0113000 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,			
	},							
	.dma_mask	= ~0,					
	.irq		= KMI1_IRQ,				
};

static struct amba_device *amba_devs[] __initdata = {
	&kmi0_device,
	&kmi1_device,
};

/* Create the amba devices to match the PL050 PS2 devices */

static __init int xilinx_ps2_init(void)
{
	int i;

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	return 0;
}
device_initcall(xilinx_ps2_init);

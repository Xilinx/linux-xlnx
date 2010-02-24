/* arch/arm/mach-xilinx/xilinx.c
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <linux/io.h>
#include <asm/hardware/gic.h>
#include <mach/hardware.h>
#include <mach/uart.h>
#include <mach/common.h>

#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>

extern struct sys_timer xttcpss_sys_timer;
extern void platform_device_init(void);

/* used by entry-macro.S */
void __iomem *gic_cpu_base_addr;

static struct at24_platform_data board_eeprom = {
	.byte_len = 8*1024,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info i2c_devs[] __initdata = {
	{ 
		I2C_BOARD_INFO("24c64", 0x50), 
		.platform_data = &board_eeprom,
	},
};

#ifndef CONFIG_SPI_SPIDEV

static struct spi_eeprom at25640 = {
        .name           = "at25LC640",
        .byte_len       = 8*1024,
        .page_size      = 32,
        .flags          = EE_ADDR2,
};

static struct spi_board_info spi_devs[] __initdata = {
        {
                .modalias = "at25",
                .max_speed_hz = 5000000,
                .bus_num = 0,
                .chip_select = 0,
                .platform_data = &at25640,
        },
};

#endif

/**
 * board_init - Board specific initialization for the Xilinx BSP.
 *
 **/
static void __init board_init(void)
{
	xilinx_debug("->board_init\n");

	platform_device_init();

	i2c_register_board_info(0, i2c_devs, ARRAY_SIZE(i2c_devs));

#ifndef CONFIG_SPI_SPIDEV
	spi_register_board_info(spi_devs,
 			         ARRAY_SIZE(spi_devs));
#endif

	xilinx_debug("<-board_init\n");
}

/**
 * irq_init - Interrupt controller initialization for the Xilinx BSP.
 *
 **/
static void __init irq_init(void)
{
	xilinx_debug("->irq_init\n");

	gic_cpu_base_addr = (void __iomem *)SCU_GIC_CPU_BASE;
	gic_dist_init(0, (void __iomem *)SCU_GIC_DIST_BASE, IRQ_GIC_START); 
	gic_cpu_init(0, gic_cpu_base_addr);

	xilinx_debug("<-irq_init\n");
}

/* The minimum devices needed to be mapped before the VM system is up and running
   include the GIC, UART and Timer Counter. Some of the devices are on the shared 
   bus (default) while others are on the private bus (non-shared).
 */

static struct map_desc io_desc[] __initdata = {
	{
		.virtual	= TTC0_BASE,
		.pfn		= __phys_to_pfn(TTC0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= SCU_PERIPH_BASE,
		.pfn		= __phys_to_pfn(SCU_PERIPH_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, 


#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= UART0_BASE,
		.pfn		= __phys_to_pfn(UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#endif

};

/**
 * map_io - Create memory mappings needed for minimal BSP.
 *
 **/
static void __init map_io(void)
{
	xilinx_debug("->map_io\n");

	iotable_init(io_desc, ARRAY_SIZE(io_desc));

#ifdef CONFIG_DEBUG_LL

	/* call this very early before the kernel early console is enabled */

	xilinx_debug("Xilinx early UART initialized\n");
	xilinx_uart_init();	
#endif

	xilinx_debug("<-map_io\n");
}

/* Xilinx uses a probe to load the kernel such that ATAGs are not setup.
 * The boot parameters in the machine description below are set to zero 
 * so that that the default ATAGs will be used in setup.c. Defaults could 
 * be defined here and pointed to also.
 */

MACHINE_START(XILINX, "Xilinx Pele A9 Emulation Platform")
	.phys_io	= IO_BASE,
	.io_pg_offst	= ((IO_BASE) >> 18) & 0xfffc,
	.boot_params    = 0,
	.map_io         = map_io,
	.init_irq       = irq_init,
	.init_machine   = board_init,
	.timer          = &xttcpss_sys_timer,
MACHINE_END

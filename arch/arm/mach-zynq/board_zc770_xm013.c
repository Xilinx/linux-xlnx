/*
 *  Copyright (C) 2011 Xilinx
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
#include <linux/of_platform.h>

#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/xilinx_devices.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include "common.h"

#define IRQ_QSPI0		51
#define IRQ_SPI1		58

#ifdef CONFIG_SPI_SPIDEV

static struct xspi_platform_data spi_0_pdata = {
	.speed_hz = 40000000,
	.bus_num = 0,
	.num_chipselect = 1
};

#else 

static struct spi_eeprom at25640_0 = {
        .name           = "at25LC640",
        .byte_len       = 8*1024,
        .page_size      = 32,
        .flags          = EE_ADDR2,
};

static struct spi_board_info spi_devs[] __initdata = {
        {
                .modalias = "at25",
                .max_speed_hz = 40000000,
                .bus_num = 0,
                .chip_select = 1,
                .platform_data = &at25640_0,
        },
};

#endif

extern struct sys_timer xttcpss_sys_timer;

static void __init board_zc770_xm013_init(void)
{

	/* initialize the xilinx common code before the board
	 * specific
	 */
	xilinx_init_machine();


#ifndef CONFIG_SPI_SPIDEV
	spi_register_board_info(&spi_devs[0], 
		ARRAY_SIZE(spi_devs));
#endif
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-zc770-xm013",
	NULL
};

MACHINE_START(XILINX, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.init_machine	= board_zc770_xm013_init,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
	.reserve	= xilinx_memory_init,
MACHINE_END

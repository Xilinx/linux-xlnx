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

#include <asm/hardware/gic.h>
#include "common.h"

#define IRQ_QSPI0		51
#define IRQ_SPI1		81

#ifdef CONFIG_SPI_SPIDEV

static struct xspi_platform_data spi_0_pdata = {
	.speed_hz = 75000000,
	.bus_num = 0,
	.num_chipselect = 1
};

static struct xspi_platform_data xqspi_0_pdata = {
	.speed_hz = 50000000,
	.bus_num = 1,
	.num_chipselect = 1
};

#endif

#ifdef CONFIG_MTD_M25P80

static struct mtd_partition spi_flash_partitions[] = {
	{
		.name		= "spi-flash",
		.size		= 0x100000,
		.offset		= 0,
	},
};

static struct flash_platform_data spi_flash_pdata = {
	.name			= "serial_flash",
	.parts			= spi_flash_partitions,
	.nr_parts		= ARRAY_SIZE(spi_flash_partitions),
	.type			= "sst25wf080"	
};


static struct mtd_partition qspi_flash_partitions[] = {
	{
		.name		= "qspi-fsbl",
		.size		= 0x80000,
		.offset		= 0,
	},
	{
		.name		= "qspi-u-boot",
		.size		= 0x80000,
		.offset		= 0x80000,
	},
	{
		.name		= "qspi-linux",
		.size		= 0x500000,
		.offset		= 0x100000,
	},
	{
		.name		= "qspi-device-tree",
		.size		= 0x20000,
		.offset		= 0x600000,
	},
	{
		.name		= "qspi-user",
		.size		= 0xE0000,
		.offset		= 0x620000,
	},
	{
		.name		= "qspi-scratch",
		.size		= 0x100000,
		.offset		= 0x700000,
	},
	{
		.name		= "qspi-rootfs",
		.size		= 0x800000,
		.offset		= 0x800000,
	},
};

static struct flash_platform_data qspi_flash_pdata = {
	.name			= "serial_flash",
	.parts			= qspi_flash_partitions,
	.nr_parts		= ARRAY_SIZE(qspi_flash_partitions),
	.type			= "n25q128"	/* single flash device */
};

#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_MTD_M25P80)

static struct spi_board_info __initdata xilinx_spipss_0_boardinfo[] = {
	{
#ifdef CONFIG_SPI_SPIDEV
		.modalias		= "spidev",
		.platform_data		= &spi_0_pdata,
#else 
		.modalias		= "m25p80",
		.platform_data		= &spi_flash_pdata,
#endif
		.irq			= IRQ_SPI1,
		.max_speed_hz		= 40000000, /* max sample rate at 3V */
		.bus_num		= 0,
		.chip_select		= 1,
	},
	{	
#ifdef CONFIG_SPI_SPIDEV
		.modalias		= "spidev",
		.platform_data		= &xqspi_0_pdata,
#else
		.modalias		= "m25p80",
		.platform_data		= &qspi_flash_pdata,
#endif
		.irq			= IRQ_QSPI0,
		.max_speed_hz		= 50000000, /* max sample rate at 3V */
		.bus_num		= 1,
		.chip_select		= 0,
	},
	
};

#endif

extern struct sys_timer xttcpss_sys_timer;

static void __init board_zc770_xm010_init(void)
{

	/* initialize the xilinx common code before the board
	 * specific
	 */
	xilinx_init_machine();

#if 	defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_MTD_M25P80)
	spi_register_board_info(&xilinx_spipss_0_boardinfo[0], 
		ARRAY_SIZE(xilinx_spipss_0_boardinfo));
#endif
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-zc770-xm010",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.handle_irq	= gic_handle_irq,
	.init_machine	= board_zc770_xm010_init,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
	.reserve	= xilinx_memory_init,
MACHINE_END

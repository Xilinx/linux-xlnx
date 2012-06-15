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
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/xilinx_devices.h>
#include <linux/of_platform.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/smc.h>
#include <mach/uart.h>

#include <asm/hardware/gic.h>
#include "common.h"

#define SMC_BASE		(0xE000E000)
#define SRAM_BASE		(0xE4000000)
#define IRQ_QSPI0		51

/* Register values for using NOR interface of SMC Controller */
#define NOR_SET_CYCLES ((0x0 << 20) | /* set_t6 or we_time from sram_cycles */ \
			(0x1 << 17) | /* set_t5 or t_tr from sram_cycles */    \
			(0x2 << 14) | /* set_t4 or t_pc from sram_cycles */    \
			(0x5 << 11) | /* set_t3 or t_wp from sram_cycles */    \
			(0x2 << 8)  | /* set_t2 t_ceoe from sram_cycles */     \
			(0x7 << 4)  | /* set_t1 t_wc from sram_cycles */       \
			(0x7))	      /* set_t0 t_rc from sram_cycles */

#define NOR_SET_OPMODE ((0x1 << 13) | /* set_burst_align,set to 32 beats */    \
			(0x1 << 12) | /* set_bls,set to default */	       \
			(0x0 << 11) | /* set_adv bit, set to default */	       \
			(0x0 << 10) | /* set_baa, we don't use baa_n */	       \
			(0x0 << 7)  | /* set_wr_bl,write brust len,set to 0 */ \
			(0x0 << 6)  | /* set_wr_sync, set to 0 */	       \
			(0x0 << 3)  | /* set_rd_bl,read brust len,set to 0 */  \
			(0x0 << 2)  | /* set_rd_sync, set to 0 */	       \
			(0x0))	      /* set_mw, memory width, 16bits width*/
				      /* 0x00002000 */
#define NOR_DIRECT_CMD ((0x0 << 23) | /* Chip 0 from interface 0 */	       \
			(0x2 << 21) | /* UpdateRegs operation */	       \
			(0x0 << 20) | /* No ModeReg write */		       \
			(0x0))	      /* Addr, not used in UpdateRegs */
				      /* 0x01400000 */

/* Register values for using SRAM interface of SMC Controller */
#define SRAM_SET_CYCLES (0x00125155)
#define SRAM_SET_OPMODE (0x00003000)
#define SRAM_DIRECT_CMD (0x00C00000)	/* Chip 1 */

extern struct sys_timer xttcpss_sys_timer;
extern void platform_device_init(void);
extern void xusbps_init(void);

/* SRAM base address */
void __iomem *xsram_base;

#ifdef CONFIG_SPI_SPIDEV

static struct xspi_platform_data xqspi_0_pdata = {
	.speed_hz = 100000000,
	.bus_num = 2,
	.num_chipselect = 1
};

static struct spi_board_info __initdata xilinx_qspipss_0_boardinfo = {
	.modalias		= "spidev",
	.platform_data		= &xqspi_0_pdata,
	.irq			= IRQ_QSPI0,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 2,
	.chip_select		= 0,
};

#else

static struct spi_eeprom at25640_0 = {
        .name           = "at25LC640",
        .byte_len       = 8*1024,
        .page_size      = 32,
        .flags          = EE_ADDR2,
};

static struct spi_eeprom at25640_1 = {
        .name           = "at25LC640",
        .byte_len       = 8*1024,
        .page_size      = 32,
        .flags          = EE_ADDR2,
};

static struct spi_board_info spi_devs[] __initdata = {
        {
                .modalias = "at25",
                .max_speed_hz = 1000000,
                .bus_num = 0,
                .chip_select = 1,
                .platform_data = &at25640_0,
        },
        {
                .modalias = "at25",
                .max_speed_hz = 1000000,
                .bus_num = 1,
                .chip_select = 1,
                .platform_data = &at25640_1,
        },
};

#ifdef CONFIG_MTD_M25P80

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

static struct spi_board_info __initdata xilinx_qspipss_0_boardinfo = {
	.modalias		= "m25p80",
	.platform_data		= &qspi_flash_pdata,
	.irq			= IRQ_QSPI0,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 2,
	.chip_select		= 0,
};

#endif	/* CONFIG_MTD_M25P80 */

#endif	/* !CONFIG_SPI_SPIDEV */

/**
 * smc_init_nor - Initialize the NOR flash interface of the SMC.
 *
 **/
#ifdef CONFIG_MTD_PHYSMAP
static void smc_init_nor(void __iomem *smc_base)
{
	__raw_writel(NOR_SET_CYCLES, smc_base + XSMCPSS_MC_SET_CYCLES);
	__raw_writel(NOR_SET_OPMODE, smc_base + XSMCPSS_MC_SET_OPMODE);
	__raw_writel(NOR_DIRECT_CMD, smc_base + XSMCPSS_MC_DIRECT_CMD);
}
#endif

/**
 * smc_init_sram - Initialize the SRAM interface of the SMC.
 *
 **/
static void smc_init_sram(void __iomem *smc_base)
{
	__raw_writel(SRAM_SET_CYCLES, smc_base + XSMCPSS_MC_SET_CYCLES);
	__raw_writel(SRAM_SET_OPMODE, smc_base + XSMCPSS_MC_SET_OPMODE);
	__raw_writel(SRAM_DIRECT_CMD, smc_base + XSMCPSS_MC_DIRECT_CMD);
}

static void __init board_ep107_init(void)
{
	void __iomem *smc_base;

	/* initialize the xilinx common code before the board
	 * specific
	 */
	xilinx_init_machine();

#ifndef CONFIG_SPI_SPIDEV
	spi_register_board_info(spi_devs, ARRAY_SIZE(spi_devs));
#endif

#if 	defined(CONFIG_SPI_SPIDEV) || \
	(!defined(CONFIG_SPI_SPIDEV) && defined(CONFIG_MTD_M25P80))
	spi_register_board_info(&xilinx_qspipss_0_boardinfo, 1);
#endif

	smc_base = ioremap(SMC_BASE, SZ_256);

#ifdef CONFIG_MTD_PHYSMAP
	smc_init_nor(smc_base);
#endif

	smc_init_sram(smc_base);
	xsram_base = ioremap(SRAM_BASE, SZ_256K);
	pr_info("SRAM at 0x%X mapped to 0x%X\n", SRAM_BASE,
		(unsigned int)xsram_base);
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-ep107",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.handle_irq	= gic_handle_irq,
	.init_machine	= board_ep107_init,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
	.reserve	= xilinx_memory_init,
MACHINE_END

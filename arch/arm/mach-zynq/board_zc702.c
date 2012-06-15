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

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/xilinx_devices.h>
#include <linux/i2c/pca954x.h>
#include <linux/i2c/pca953x.h>
#include <linux/i2c/si570.h>
#include <linux/gpio.h>

#include <mach/slcr.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

#include "common.h"

#define IRQ_SPI1		81
#define USB_RST_GPIO	7

#ifdef CONFIG_SPI_SPIDEV

static struct xspi_platform_data spi_0_pdata = {
	.speed_hz = 75000000,
	.bus_num = 0,
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

#endif

#if defined(CONFIG_I2C_XILINX_PS) && defined(CONFIG_I2C_MUX_PCA954x)

static struct pca954x_platform_mode pca954x_platform_modes[] = {
	{
		.adap_id 		= 1,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 2,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 3,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 4,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 5,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 6,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 7,
		.deselect_on_exit	= 0,
	},
	{
		.adap_id 		= 8,
		.deselect_on_exit	= 0,
	},
};

static struct pca954x_platform_data pca954x_i2cmux_adap_data = {
	.modes 		= pca954x_platform_modes,
	.num_modes 	= 8,
};

static struct i2c_board_info __initdata pca954x_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pca9548", 0x74),
		.platform_data = &pca954x_i2cmux_adap_data,
	},
};

#if defined(CONFIG_RTC_DRV_PCF8563)

static struct i2c_board_info __initdata rtc8564_board_info[] = {
	{
		I2C_BOARD_INFO("rtc8564", 0x51),
	},
};

#endif /*CONFIG_RTC_DRV_PCF8563 */

#if defined(CONFIG_GPIO_PCA953X)

static struct pca953x_platform_data tca6416_0 = {
	.gpio_base = 256,
};

static struct i2c_board_info __initdata tca6416_board_info[] = {
	{
		I2C_BOARD_INFO("tca6416", 0x21),
		.platform_data = &tca6416_0,
	}
};

#endif /* CONFIG_GPIO_PCF8563 */

#if defined(CONFIG_SI570)

/* Initial FOUT is set per the ADV7511 video clocking requirement */
static struct si570_platform_data si570_0 = {
	.factory_fout = 156250000LL,
	.initial_fout = 148500000,
};

static struct i2c_board_info __initdata si570_board_info[] = {
	{
		I2C_BOARD_INFO("si570", 0x5d),
		.platform_data = &si570_0,
	}
};

#endif /* CONFIG_SI570 */
	
#if defined(CONFIG_EEPROM_AT24)

static struct i2c_board_info __initdata m24c08_board_info[] = {
	{
		I2C_BOARD_INFO("24c08", 0x54),
	},
};

#endif /* CONFIG_EEPROM_AT24 */

#endif /* CONFIG_I2C_XILINX_PS && CONFIG_I2C_MUX_PCA954x */

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
};

#endif

extern struct sys_timer xttcpss_sys_timer;

static void __init board_zc702_init(void)
{

	/* initialize the xilinx common code before the board
	 * specific
	 */
	xilinx_init_machine();

	/* Reset USB by toggling MIO7 */
	if (gpio_request(USB_RST_GPIO, "USB Reset"))
		printk(KERN_ERR "ERROR requesting GPIO, USB not reset!");

	if (gpio_direction_output(USB_RST_GPIO, 1))
		printk(KERN_ERR "ERROR setting GPIO direction, USB not reset!");

	gpio_set_value(USB_RST_GPIO, 1);
	gpio_set_value(USB_RST_GPIO, 0);
	gpio_set_value(USB_RST_GPIO, 1);

#if 	defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_MTD_M25P80)
	spi_register_board_info(&xilinx_spipss_0_boardinfo[0], 
		ARRAY_SIZE(xilinx_spipss_0_boardinfo));
#endif

#if	defined(CONFIG_I2C_XILINX_PS) && defined(CONFIG_I2C_MUX_PCA954x)
	i2c_register_board_info(0, pca954x_i2c_devices,
				ARRAY_SIZE(pca954x_i2c_devices));

#if	defined(CONFIG_SI570)
	i2c_register_board_info(1, si570_board_info,
				ARRAY_SIZE(si570_board_info));
#endif

#if	defined(CONFIG_EEPROM_AT24)
	i2c_register_board_info(3, m24c08_board_info,
				ARRAY_SIZE(m24c08_board_info));
#endif

#if	defined(CONFIG_GPIO_PCA953X)
	i2c_register_board_info(4, tca6416_board_info,
				ARRAY_SIZE(tca6416_board_info));
#endif

#if	defined(CONFIG_RTC_DRV_PCF8563)
	i2c_register_board_info(5, rtc8564_board_info,
				ARRAY_SIZE(rtc8564_board_info));
#endif


#endif
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-zc702",
	"xlnx,zynq-zc706",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.handle_irq	= gic_handle_irq,
	.init_machine	= board_zc702_init,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
	.reserve	= xilinx_memory_init,
	.restart	= xilinx_system_reset,
MACHINE_END

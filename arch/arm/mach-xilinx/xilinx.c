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
#include <asm/hardware/cache-l2x0.h>
#include <mach/hardware.h>
#include <mach/uart.h>
#include <mach/common.h>
#include <mach/smc.h>

#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>

#include <linux/of_platform.h>

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

#define L2_TAG_LATENCY (0x111)
#define L2_DATA_LATENCY (0x111)

extern struct sys_timer xttcpss_sys_timer;
extern void platform_device_init(void);

/* SRAM base address */
void __iomem *xsram_base;

/* The 1st I2C bus has an eeprom and a real time clock on
   it.
*/
static struct i2c_board_info i2c_devs_0[] __initdata = {
	{
		I2C_BOARD_INFO("24c02", 0x50),
	},
	{
		I2C_BOARD_INFO("rtc8564", 0x51),
	},
};

/* The 2nd I2C bus has an eeprom on it also.
*/
static struct i2c_board_info i2c_devs_1[] __initdata = {
	{
		I2C_BOARD_INFO("24c02", 0x55),
	},
};


#ifndef CONFIG_SPI_SPIDEV

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
                .chip_select = 0,
                .platform_data = &at25640_0,
        },
        {
                .modalias = "at25",
                .max_speed_hz = 1000000,
                .bus_num = 1,
                .chip_select = 0,
                .platform_data = &at25640_1,
        },
};

#endif

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

/**
 * board_init - Board specific initialization for the Xilinx BSP.
 *
 **/

static struct of_device_id xilinx_of_bus_ids[] __initdata = {
        { .compatible = "simple-bus", },
        {}
};

static void __init board_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	void *l2cache_base;
#endif

	void __iomem *smc_base;

	pr_debug("->board_init\n");

	platform_device_init();
#ifdef CONFIG_OF
	pr_info("Xilinx: using device tree\n");
        of_platform_bus_probe(NULL, xilinx_of_bus_ids, NULL);
#endif

#ifdef CONFIG_CACHE_L2X0
	/* Static mapping, never released */
	l2cache_base = ioremap(PL310_L2CC_BASE, SZ_4K);
	BUG_ON(!l2cache_base);

	__raw_writel(L2_TAG_LATENCY, l2cache_base + L2X0_TAG_LATENCY_CTRL);
	__raw_writel(L2_DATA_LATENCY, l2cache_base + L2X0_TAG_LATENCY_CTRL);
	printk(KERN_INFO "l2x0: Tag Latency set to 0x%X cycles\n", L2_TAG_LATENCY);
	printk(KERN_INFO "l2x0: Data Latency set to 0x%X cycles\n", L2_DATA_LATENCY);

	/*
	 * 64KB way size, 8-way associativity, parity disabled
	 */
	l2x0_init(l2cache_base, 0x02060000, 0xF0F0FFFF);
#endif

	i2c_register_board_info(0, i2c_devs_0, ARRAY_SIZE(i2c_devs_0));
	i2c_register_board_info(1, i2c_devs_1, ARRAY_SIZE(i2c_devs_1));

#ifndef CONFIG_SPI_SPIDEV
	spi_register_board_info(spi_devs,
 			         ARRAY_SIZE(spi_devs));
#endif

	smc_base = ioremap(SMC_BASE, SZ_256);

#ifdef CONFIG_MTD_PHYSMAP
	smc_init_nor(smc_base);
#endif

	smc_init_sram(smc_base);
	xsram_base = ioremap(SRAM_BASE, SZ_256K);
	pr_info("SRAM at 0x%X mapped to 0x%X\n", SRAM_BASE,
		(unsigned int)xsram_base);

	pr_debug("<-board_init\n");
}

/**
 * irq_init - Interrupt controller initialization for the Xilinx BSP.
 *
 **/
static void __init irq_init(void)
{
	pr_debug("->irq_init\n");

	gic_cpu_base_addr = (void __iomem *)SCU_GIC_CPU_BASE;

	gic_init(0, 29, (void __iomem *)SCU_GIC_DIST_BASE, gic_cpu_base_addr);

	pr_debug("<-irq_init\n");
}

/* The minimum devices needed to be mapped before the VM system is up and running
   include the GIC, UART and Timer Counter. Some of the devices are on the shared
   bus (default) while others are on the private bus (non-shared). The boot
   register addresses are also setup at this time so that SMP processing can use
   them.
 */

static struct map_desc io_desc[] __initdata = {
	{
		.virtual	= TTC0_BASE,
		.pfn		= __phys_to_pfn(TTC0_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= SCU_PERIPH_BASE,
		.pfn		= __phys_to_pfn(SCU_PERIPH_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_SMP
	{
		.virtual	= BOOT_REG_BASE,
		.pfn		= __phys_to_pfn(BOOT_REG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#endif

#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= UART0_BASE,
		.pfn		= __phys_to_pfn(UART0_BASE),
		.length		= SZ_8K,
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
	pr_debug("->map_io\n");

	iotable_init(io_desc, ARRAY_SIZE(io_desc));

#ifdef CONFIG_DEBUG_LL

	/* call this very early before the kernel early console is enabled */

	pr_debug("Xilinx early UART initialized\n");
	xilinx_uart_init();
#endif

	pr_debug("<-map_io\n");
}

static const char * xilinx_peep_board_compat[] = {
	"xlnx,arm-ep",
	NULL
};

/* Xilinx uses a probe to load the kernel such that ATAGs are not setup.
 * The boot parameters in the machine description below are set to zero
 * so that that the default ATAGs will be used in setup.c. Defaults could
 * be defined here and pointed to also.
 */

MACHINE_START(XILINX, "Xilinx Pele A9 Emulation Platform")
	.boot_params    = 0,
	.map_io         = map_io,
	.init_irq       = irq_init,
	.init_machine   = board_init,
	.timer          = &xttcpss_sys_timer,
	.dt_compat      = xilinx_peep_board_compat,
MACHINE_END

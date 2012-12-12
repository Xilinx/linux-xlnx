/*
 * Zynq clock initalization code
 * Code is based on clock code from the orion/kirkwood architecture.
 *
 *  Copyright (C) 2012 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <mach/zynq_soc.h>
#include <mach/clk.h>

static DEFINE_SPINLOCK(armpll_lock);
static DEFINE_SPINLOCK(ddrpll_lock);
static DEFINE_SPINLOCK(iopll_lock);
static DEFINE_SPINLOCK(armclk_lock);
static DEFINE_SPINLOCK(ddrclk_lock);
static DEFINE_SPINLOCK(dciclk_lock);
/*
 * static DEFINE_SPINLOCK(smcclk_lock);
 * static DEFINE_SPINLOCK(pcapclk_lock);
 */
static DEFINE_SPINLOCK(lqspiclk_lock);
static DEFINE_SPINLOCK(gem0clk_lock);
static DEFINE_SPINLOCK(gem1clk_lock);
static DEFINE_SPINLOCK(fpga0clk_lock);
static DEFINE_SPINLOCK(fpga1clk_lock);
static DEFINE_SPINLOCK(fpga2clk_lock);
static DEFINE_SPINLOCK(fpga3clk_lock);
static DEFINE_SPINLOCK(canclk_lock);
static DEFINE_SPINLOCK(sdioclk_lock);
static DEFINE_SPINLOCK(uartclk_lock);
static DEFINE_SPINLOCK(spiclk_lock);
static DEFINE_SPINLOCK(dbgclk_lock);
static DEFINE_SPINLOCK(aperclk_lock);


/* Clock parent arrays */
static const char *cpu_parents[] __initdata = {"ARMPLL", "ARMPLL",
	"DDRPLL", "IOPLL"};
static const char *def_periph_parents[] __initdata = {"IOPLL", "IOPLL",
	"ARMPLL", "DDRPLL"};
static const char *gem_parents[] __initdata = {"IOPLL", "IOPLL", "ARMPLL",
	"DDRPLL", "GEM0EMIO", "GEM0EMIO", "GEM0EMIO", "GEM0EMIO"};
static const char *dbg_parents[] __initdata = {"IOPLL", "IOPLL", "ARMPLL",
	"DDRPLL", "DBGEMIOTRC", "DBGEMIOTRC", "DBGEMIOTRC", "DBGEMIOTRC"};
static const char *dci_parents[] __initdata = {"DDRPLL"};
static const char *clk621_parents[] __initdata = {"CPU_MASTER_CLK"};

/**
 * zynq_clkdev_add() - Add a clock device
 * @con_id: Connection identifier
 * @dev_id: Device identifier
 * @clk: Struct clock to associate with given connection and device.
 *
 * Create a clkdev entry for a given device/clk
 */
static void __init zynq_clkdev_add(const char *con_id, const char *dev_id,
		struct clk *clk)
{
	if (clk_register_clkdev(clk, con_id, dev_id))
		pr_warn("Adding clkdev failed.");
}

static const struct of_device_id matches __initconst = {
	.compatible = "xlnx,zynq",
	.name = "soc",
};

/**
 * zynq_clock_init() - Clock initalization
 *
 * Register clocks and clock devices with the common clock framework.
 * To avoid enabling unused clocks, only leaf clocks are present for which the
 * drivers supports the common clock framework.
 */
void __init zynq_clock_init(void)
{
	struct clk *clk;
	struct device_node *np;
	const void *prop;
	unsigned int ps_clk_f = 33333333;

	pr_info("Zynq clock init\n");

	np = of_find_matching_node(NULL, &matches);
	if (np) {
		prop = of_get_property(np, "clock-frequency", NULL);
		if (prop)
			ps_clk_f = be32_to_cpup(prop);
		of_node_put(np);
	}

	clk = clk_register_fixed_rate(NULL, "PS_CLK", NULL, CLK_IS_ROOT,
			ps_clk_f);
	clk = clk_register_zynq_pll("ARMPLL",
			(__force void __iomem *)SLCR_ARMPLL_CTRL,
			(__force void __iomem *)SLCR_ARMPLL_CFG,
			(__force void __iomem *)SLCR_PLL_STATUS,
			0, &armpll_lock);
	clk = clk_register_zynq_pll("DDRPLL",
			(__force void __iomem *)SLCR_DDRPLL_CTRL,
			(__force void __iomem *)SLCR_DDRPLL_CFG,
			(__force void __iomem *)SLCR_PLL_STATUS,
			1, &ddrpll_lock);
	clk = clk_register_zynq_pll("IOPLL",
			(__force void __iomem *)SLCR_IOPLL_CTRL,
			(__force void __iomem *)SLCR_IOPLL_CFG,
			(__force void __iomem *)SLCR_PLL_STATUS,
			2, &iopll_lock);

	/* CPU clocks */
	clk = clk_register_zynq_d1m("CPU_MASTER_CLK",
			(__force void __iomem *)SLCR_ARM_CLK_CTRL,
			cpu_parents, 4, &armclk_lock);
	clk = clk_register_gate(NULL, "CPU_6OR4X_CLK", "CPU_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_ARM_CLK_CTRL,
			24, 0, &armclk_lock);
	zynq_clkdev_add(NULL, "CPU_6OR4X_CLK", clk);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_fixed_factor(NULL, "CPU_3OR2X_DIV_CLK",
			"CPU_MASTER_CLK", 0, 1, 2);
	clk = clk_register_gate(NULL, "CPU_3OR2X_CLK", "CPU_3OR2X_DIV_CLK", 0,
			(__force void __iomem *)SLCR_ARM_CLK_CTRL, 25, 0,
			&armclk_lock);
	zynq_clkdev_add(NULL, "smp_twd", clk);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_zynq_clk621("CPU_1X_DIV_CLK",
		(__force void __iomem *)SLCR_ARM_CLK_CTRL,
		(__force void __iomem *)SLCR_621_TRUE, 4, 2, clk621_parents, 1,
		&armclk_lock);
	clk = clk_register_zynq_clk621("CPU_2X_DIV_CLK",
		(__force void __iomem *)SLCR_ARM_CLK_CTRL,
		(__force void __iomem *)SLCR_621_TRUE, 2, 1, clk621_parents, 1,
		&armclk_lock);
	clk = clk_register_gate(NULL, "CPU_2X_CLK", "CPU_2X_DIV_CLK", 0,
			(__force void __iomem *)SLCR_ARM_CLK_CTRL, 26, 0,
			&armclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_gate(NULL, "CPU_1X_CLK", "CPU_1X_DIV_CLK", 0,
			(__force void __iomem *)SLCR_ARM_CLK_CTRL, 27, 0,
			&armclk_lock);
	zynq_clkdev_add(NULL, "CPU_1X_CLK", clk);
	clk_prepare(clk);
	clk_enable(clk);
	/* DDR clocks */
	clk = clk_register_divider(NULL, "DDR_2X_DIV_CLK", "DDRPLL", 0,
			(__force void __iomem *)SLCR_DDR_CLK_CTRL, 26, 6,
			CLK_DIVIDER_ONE_BASED, &ddrclk_lock);
	clk = clk_register_gate(NULL, "DDR_2X_CLK", "DDR_2X_DIV_CLK", 0,
			(__force void __iomem *)SLCR_DDR_CLK_CTRL, 1, 0,
			&ddrclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_divider(NULL, "DDR_3X_DIV_CLK", "DDRPLL", 0,
			(__force void __iomem *)SLCR_DDR_CLK_CTRL, 20, 6,
			CLK_DIVIDER_ONE_BASED, &ddrclk_lock);
	clk = clk_register_gate(NULL, "DDR_3X_CLK", "DDR_3X_DIV_CLK", 0,
			(__force void __iomem *)SLCR_DDR_CLK_CTRL, 0, 0,
			&ddrclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_zynq_gd2m("DCI_CLK",
			(__force void __iomem *)SLCR_DCI_CLK_CTRL,
			dci_parents, 1, &dciclk_lock);
	clk_prepare(clk);
	clk_enable(clk);

	/* Peripheral clocks */
	clk = clk_register_zynq_gd1m("LQSPI_CLK",
			(__force void __iomem *)SLCR_LQSPI_CLK_CTRL,
			def_periph_parents, &lqspiclk_lock);
	zynq_clkdev_add(NULL, "LQSPI", clk);

	/*
	 * clk = clk_register_zynq_gd1m("SMC_CLK",
	 *		(__force void __iomem *)SLCR_SMC_CLK_CTRL,
	 *		def_periph_parents, &smcclk_lock);
	 * zynq_clkdev_add(NULL, "SMC", clk);
	 *
	 * clk = clk_register_zynq_gd1m("PCAP_CLK",
	 *		(__force void __iomem *)SLCR_PCAP_CLK_CTRL,
	 *		def_periph_parents, &pcapclk_lock);
	 * zynq_clkdev_add(NULL, "PCAP", clk);
	 */

	clk = clk_register_zynq_gd2m("GEM0_CLK",
			(__force void __iomem *)SLCR_GEM0_CLK_CTRL,
			gem_parents, 8, &gem0clk_lock);
	zynq_clkdev_add(NULL, "GEM0", clk);
	clk = clk_register_zynq_gd2m("GEM1_CLK",
			(__force void __iomem *)SLCR_GEM1_CLK_CTRL,
			gem_parents, 8, &gem1clk_lock);
	zynq_clkdev_add(NULL, "GEM1", clk);

	clk = clk_register_zynq_d2m("FPGA0_CLK",
			(__force void __iomem *)SLCR_FPGA0_CLK_CTRL,
			def_periph_parents, &fpga0clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA0", clk);
	clk = clk_register_zynq_d2m("FPGA1_CLK",
			(__force void __iomem *)SLCR_FPGA1_CLK_CTRL,
			def_periph_parents, &fpga1clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA1", clk);
	clk = clk_register_zynq_d2m("FPGA2_CLK",
			(__force void __iomem *)SLCR_FPGA2_CLK_CTRL,
			def_periph_parents, &fpga2clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA2", clk);
	clk = clk_register_zynq_d2m("FPGA3_CLK",
			(__force void __iomem *)SLCR_FPGA3_CLK_CTRL,
			def_periph_parents, &fpga3clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA3", clk);
	clk = clk_register_zynq_d2m("CAN_MASTER_CLK",
			(__force void __iomem *)SLCR_CAN_CLK_CTRL,
			def_periph_parents, &canclk_lock);

	clk = clk_register_zynq_d1m("SDIO_MASTER_CLK",
			(__force void __iomem *)SLCR_SDIO_CLK_CTRL,
			def_periph_parents, 4, &sdioclk_lock);
	clk = clk_register_zynq_d1m("UART_MASTER_CLK",
			(__force void __iomem *)SLCR_UART_CLK_CTRL,
			def_periph_parents, 4, &uartclk_lock);
	clk = clk_register_zynq_d1m("SPI_MASTER_CLK",
			(__force void __iomem *)SLCR_SPI_CLK_CTRL,
			def_periph_parents, 4, &spiclk_lock);
	clk = clk_register_zynq_d1m("DBG_MASTER_CLK",
			(__force void __iomem *)SLCR_DBG_CLK_CTRL,
			dbg_parents, 8, &dbgclk_lock);

	/*
	 * clk = clk_register_gate(NULL, "CAN0_CLK", "CAN_MASTER_CLK",
	 *	CLK_SET_RATE_PARENT, (__force void __iomem *)SLCR_CAN_CLK_CTRL,
	 *	0, 0, &canclk_lock);
	 * zynq_clkdev_add(NULL, "CAN0", clk);
	 * clk = clk_register_gate(NULL, "CAN1_CLK", "CAN_MASTER_CLK",
	 *	CLK_SET_RATE_PARENT, (__force void __iomem *)SLCR_CAN_CLK_CTRL,
	 *	1, 0, &canclk_lock);
	 * zynq_clkdev_add(NULL, "CAN1", clk);
	 */

	clk = clk_register_gate(NULL, "SDIO0_CLK", "SDIO_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_SDIO_CLK_CTRL,
			0, 0, &sdioclk_lock);
	zynq_clkdev_add(NULL, "SDIO0", clk);
	clk = clk_register_gate(NULL, "SDIO1_CLK", "SDIO_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_SDIO_CLK_CTRL,
			1, 0, &sdioclk_lock);
	zynq_clkdev_add(NULL, "SDIO1", clk);

	clk = clk_register_gate(NULL, "UART0_CLK", "UART_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_UART_CLK_CTRL,
			0, 0, &uartclk_lock);
	zynq_clkdev_add(NULL, "UART0", clk);
	clk = clk_register_gate(NULL, "UART1_CLK", "UART_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_UART_CLK_CTRL,
			1, 0, &uartclk_lock);
	zynq_clkdev_add(NULL, "UART1", clk);

	clk = clk_register_gate(NULL, "SPI0_CLK", "SPI_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_SPI_CLK_CTRL,
			0, 0, &spiclk_lock);
	zynq_clkdev_add(NULL, "SPI0", clk);
	clk = clk_register_gate(NULL, "SPI1_CLK", "SPI_MASTER_CLK",
			CLK_SET_RATE_PARENT,
			(__force void __iomem *)SLCR_SPI_CLK_CTRL,
			1, 0, &spiclk_lock);
	zynq_clkdev_add(NULL, "SPI1", clk);
	/*
	 * clk = clk_register_gate(NULL, "DBGTRC_CLK", "DBG_MASTER_CLK",
	 *		CLK_SET_RATE_PARENT,
	 *		(__force void __iomem *)SLCR_DBG_CLK_CTRL,
	 *		0, 0, &dbgclk_lock);
	 * zynq_clkdev_add(NULL, "DBGTRC", clk);
	 * clk = clk_register_gate(NULL, "DBG1X_CLK", "DBG_MASTER_CLK",
	 *		CLK_SET_RATE_PARENT,
	 *		(__force void __iomem *)SLCR_DBG_CLK_CTRL,
	 *		1, 0, &dbgclk_lock);
	 * zynq_clkdev_add(NULL, "DBG1X", clk);
	 */

	/* One gated clock for all APER clocks. */
	/*
	 * clk = clk_register_gate(NULL, "DMA_CPU2X", "CPU_2X_CLK", 0,
	 *		(__force void __iomem *)SLCR_APER_CLK_CTRL, 0, 0,
	 *		&aperclk_lock);
	 * zynq_clkdev_add(NULL, "DMA_APER", clk);
	 */
	clk = clk_register_gate(NULL, "USB0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 2, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "USB0_APER", clk);
	clk = clk_register_gate(NULL, "USB1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 3, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "USB1_APER", clk);
	clk = clk_register_gate(NULL, "GEM0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 6, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "GEM0_APER", clk);
	clk = clk_register_gate(NULL, "GEM1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 7, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "GEM1_APER", clk);
	clk = clk_register_gate(NULL, "SDI0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 10, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "SDIO0_APER", clk);
	clk = clk_register_gate(NULL, "SDI1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 11, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "SDIO1_APER", clk);
	clk = clk_register_gate(NULL, "SPI0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 14, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "SPI0_APER", clk);
	clk = clk_register_gate(NULL, "SPI1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 15, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "SPI1_APER", clk);
	/*
	 * clk = clk_register_gate(NULL, "CAN0_CPU1X", "CPU_1X_CLK", 0,
	 *		(__force void __iomem *)SLCR_APER_CLK_CTRL, 16, 0,
	 *		&aperclk_lock);
	 * zynq_clkdev_add(NULL, "CAN0_APER", clk);
	 * clk = clk_register_gate(NULL, "CAN1_CPU1X", "CPU_1X_CLK", 0,
	 *		(__force void __iomem *)SLCR_APER_CLK_CTRL, 17, 0,
	 *		&aperclk_lock);
	 * zynq_clkdev_add(NULL, "CAN1_APER", clk);
	 */
	clk = clk_register_gate(NULL, "I2C0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 18, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "I2C0_APER", clk);
	clk = clk_register_gate(NULL, "I2C1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 19, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "I2C1_APER", clk);
	clk = clk_register_gate(NULL, "UART0_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 20, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "UART0_APER", clk);
	clk = clk_register_gate(NULL, "UART1_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 21, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "UART1_APER", clk);
	clk = clk_register_gate(NULL, "GPIO_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 22, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "GPIO_APER", clk);
	clk = clk_register_gate(NULL, "LQSPI_CPU1X", "CPU_1X_CLK", 0,
			(__force void __iomem *)SLCR_APER_CLK_CTRL, 23, 0,
			&aperclk_lock);
	zynq_clkdev_add(NULL, "LQSPI_APER", clk);
	/*
	 * clk = clk_register_gate(NULL, "SMC_CPU1X", "CPU_1X_CLK", 0,
	 *		(__force void __iomem *)SLCR_APER_CLK_CTRL, 24, 0,
	 *		&aperclk_lock);
	 * zynq_clkdev_add(NULL, "SMC_APER", clk);
	 */
}

/*
 * arch/arm/mach-kirkwood/common.c
 *
 * Core functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/ata_platform.h>
#include <linux/mtd/nand.h>
#include <linux/dma-mapping.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/mv643xx_i2c.h>
#include <linux/timex.h>
#include <linux/kexec.h>
#include <linux/reboot.h>
#include <net/dsa.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include <linux/platform_data/asoc-kirkwood.h>
#include <plat/cache-feroceon-l2.h>
#include <linux/platform_data/mmc-mvsdio.h>
#include <linux/platform_data/mtd-orion_nand.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <plat/common.h>
#include <plat/time.h>
#include <linux/platform_data/dma-mv_xor.h>
#include "common.h"

/* These can go away once Kirkwood uses the mvebu-mbus DT binding */
#define KIRKWOOD_MBUS_NAND_TARGET 0x01
#define KIRKWOOD_MBUS_NAND_ATTR   0x2f
#define KIRKWOOD_MBUS_SRAM_TARGET 0x03
#define KIRKWOOD_MBUS_SRAM_ATTR   0x01

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc kirkwood_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) KIRKWOOD_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(KIRKWOOD_REGS_PHYS_BASE),
		.length		= KIRKWOOD_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init kirkwood_map_io(void)
{
	iotable_init(kirkwood_io_desc, ARRAY_SIZE(kirkwood_io_desc));
}

/*****************************************************************************
 * CLK tree
 ****************************************************************************/

static void enable_sata0(void)
{
	/* Enable PLL and IVREF */
	writel(readl(SATA0_PHY_MODE_2) | 0xf, SATA0_PHY_MODE_2);
	/* Enable PHY */
	writel(readl(SATA0_IF_CTRL) & ~0x200, SATA0_IF_CTRL);
}

static void disable_sata0(void)
{
	/* Disable PLL and IVREF */
	writel(readl(SATA0_PHY_MODE_2) & ~0xf, SATA0_PHY_MODE_2);
	/* Disable PHY */
	writel(readl(SATA0_IF_CTRL) | 0x200, SATA0_IF_CTRL);
}

static void enable_sata1(void)
{
	/* Enable PLL and IVREF */
	writel(readl(SATA1_PHY_MODE_2) | 0xf, SATA1_PHY_MODE_2);
	/* Enable PHY */
	writel(readl(SATA1_IF_CTRL) & ~0x200, SATA1_IF_CTRL);
}

static void disable_sata1(void)
{
	/* Disable PLL and IVREF */
	writel(readl(SATA1_PHY_MODE_2) & ~0xf, SATA1_PHY_MODE_2);
	/* Disable PHY */
	writel(readl(SATA1_IF_CTRL) | 0x200, SATA1_IF_CTRL);
}

static void disable_pcie0(void)
{
	writel(readl(PCIE_LINK_CTRL) | 0x10, PCIE_LINK_CTRL);
	while (1)
		if (readl(PCIE_STATUS) & 0x1)
			break;
	writel(readl(PCIE_LINK_CTRL) & ~0x10, PCIE_LINK_CTRL);
}

static void disable_pcie1(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if (dev == MV88F6282_DEV_ID) {
		writel(readl(PCIE1_LINK_CTRL) | 0x10, PCIE1_LINK_CTRL);
		while (1)
			if (readl(PCIE1_STATUS) & 0x1)
				break;
		writel(readl(PCIE1_LINK_CTRL) & ~0x10, PCIE1_LINK_CTRL);
	}
}

/* An extended version of the gated clk. This calls fn_en()/fn_dis
 * before enabling/disabling the clock.  We use this to turn on/off
 * PHYs etc.  */
struct clk_gate_fn {
	struct clk_gate gate;
	void (*fn_en)(void);
	void (*fn_dis)(void);
};

#define to_clk_gate_fn(_gate) container_of(_gate, struct clk_gate_fn, gate)
#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static int clk_gate_fn_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct clk_gate_fn *gate_fn = to_clk_gate_fn(gate);
	int ret;

	ret = clk_gate_ops.enable(hw);
	if (!ret && gate_fn->fn_en)
		gate_fn->fn_en();

	return ret;
}

static void clk_gate_fn_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct clk_gate_fn *gate_fn = to_clk_gate_fn(gate);

	if (gate_fn->fn_dis)
		gate_fn->fn_dis();

	clk_gate_ops.disable(hw);
}

static struct clk_ops clk_gate_fn_ops;

static struct clk __init *clk_register_gate_fn(struct device *dev,
		const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate_flags, spinlock_t *lock,
		void (*fn_en)(void), void (*fn_dis)(void))
{
	struct clk_gate_fn *gate_fn;
	struct clk *clk;
	struct clk_init_data init;

	gate_fn = kzalloc(sizeof(struct clk_gate_fn), GFP_KERNEL);
	if (!gate_fn) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_gate_fn_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_gate assignments */
	gate_fn->gate.reg = reg;
	gate_fn->gate.bit_idx = bit_idx;
	gate_fn->gate.flags = clk_gate_flags;
	gate_fn->gate.lock = lock;
	gate_fn->gate.hw.init = &init;
	gate_fn->fn_en = fn_en;
	gate_fn->fn_dis = fn_dis;

	/* ops is the gate ops, but with our enable/disable functions */
	if (clk_gate_fn_ops.enable != clk_gate_fn_enable ||
	    clk_gate_fn_ops.disable != clk_gate_fn_disable) {
		clk_gate_fn_ops = clk_gate_ops;
		clk_gate_fn_ops.enable = clk_gate_fn_enable;
		clk_gate_fn_ops.disable = clk_gate_fn_disable;
	}

	clk = clk_register(dev, &gate_fn->gate.hw);

	if (IS_ERR(clk))
		kfree(gate_fn);

	return clk;
}

static DEFINE_SPINLOCK(gating_lock);
static struct clk *tclk;

static struct clk __init *kirkwood_register_gate(const char *name, u8 bit_idx)
{
	return clk_register_gate(NULL, name, "tclk", 0, CLOCK_GATING_CTRL,
				 bit_idx, 0, &gating_lock);
}

static struct clk __init *kirkwood_register_gate_fn(const char *name,
						    u8 bit_idx,
						    void (*fn_en)(void),
						    void (*fn_dis)(void))
{
	return clk_register_gate_fn(NULL, name, "tclk", 0, CLOCK_GATING_CTRL,
				    bit_idx, 0, &gating_lock, fn_en, fn_dis);
}

static struct clk *ge0, *ge1;

void __init kirkwood_clk_init(void)
{
	struct clk *runit, *sata0, *sata1, *usb0, *sdio;
	struct clk *crypto, *xor0, *xor1, *pex0, *pex1, *audio;

	tclk = clk_register_fixed_rate(NULL, "tclk", NULL,
				       CLK_IS_ROOT, kirkwood_tclk);

	runit = kirkwood_register_gate("runit",  CGC_BIT_RUNIT);
	ge0 = kirkwood_register_gate("ge0",    CGC_BIT_GE0);
	ge1 = kirkwood_register_gate("ge1",    CGC_BIT_GE1);
	sata0 = kirkwood_register_gate_fn("sata0",  CGC_BIT_SATA0,
					  enable_sata0, disable_sata0);
	sata1 = kirkwood_register_gate_fn("sata1",  CGC_BIT_SATA1,
					  enable_sata1, disable_sata1);
	usb0 = kirkwood_register_gate("usb0",   CGC_BIT_USB0);
	sdio = kirkwood_register_gate("sdio",   CGC_BIT_SDIO);
	crypto = kirkwood_register_gate("crypto", CGC_BIT_CRYPTO);
	xor0 = kirkwood_register_gate("xor0",   CGC_BIT_XOR0);
	xor1 = kirkwood_register_gate("xor1",   CGC_BIT_XOR1);
	pex0 = kirkwood_register_gate_fn("pex0",   CGC_BIT_PEX0,
					 NULL, disable_pcie0);
	pex1 = kirkwood_register_gate_fn("pex1",   CGC_BIT_PEX1,
					 NULL, disable_pcie1);
	audio = kirkwood_register_gate("audio",  CGC_BIT_AUDIO);
	kirkwood_register_gate("tdm",    CGC_BIT_TDM);
	kirkwood_register_gate("tsu",    CGC_BIT_TSU);

	/* clkdev entries, mapping clks to devices */
	orion_clkdev_add(NULL, "orion_spi.0", runit);
	orion_clkdev_add(NULL, "orion_spi.1", runit);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".0", ge0);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".1", ge1);
	orion_clkdev_add(NULL, "orion_wdt", tclk);
	orion_clkdev_add("0", "sata_mv.0", sata0);
	orion_clkdev_add("1", "sata_mv.0", sata1);
	orion_clkdev_add(NULL, "orion-ehci.0", usb0);
	orion_clkdev_add(NULL, "orion_nand", runit);
	orion_clkdev_add(NULL, "mvsdio", sdio);
	orion_clkdev_add(NULL, "mv_crypto", crypto);
	orion_clkdev_add(NULL, MV_XOR_NAME ".0", xor0);
	orion_clkdev_add(NULL, MV_XOR_NAME ".1", xor1);
	orion_clkdev_add("0", "pcie", pex0);
	orion_clkdev_add("1", "pcie", pex1);
	orion_clkdev_add(NULL, "mvebu-audio", audio);
	orion_clkdev_add(NULL, MV64XXX_I2C_CTLR_NAME ".0", runit);
	orion_clkdev_add(NULL, MV64XXX_I2C_CTLR_NAME ".1", runit);

	/* Marvell says runit is used by SPI, UART, NAND, TWSI, ...,
	 * so should never be gated.
	 */
	clk_prepare_enable(runit);
}

/*****************************************************************************
 * EHCI0
 ****************************************************************************/
void __init kirkwood_ehci_init(void)
{
	orion_ehci_init(USB_PHYS_BASE, IRQ_KIRKWOOD_USB, EHCI_PHY_NA);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
void __init kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge00_init(eth_data,
			GE00_PHYS_BASE, IRQ_KIRKWOOD_GE00_SUM,
			IRQ_KIRKWOOD_GE00_ERR, 1600);
	/* The interface forgets the MAC address assigned by u-boot if
	the clock is turned off, so claim the clk now. */
	clk_prepare_enable(ge0);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
void __init kirkwood_ge01_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge01_init(eth_data,
			GE01_PHYS_BASE, IRQ_KIRKWOOD_GE01_SUM,
			IRQ_KIRKWOOD_GE01_ERR, 1600);
	clk_prepare_enable(ge1);
}


/*****************************************************************************
 * Ethernet switch
 ****************************************************************************/
void __init kirkwood_ge00_switch_init(struct dsa_platform_data *d, int irq)
{
	orion_ge00_switch_init(d, irq);
}


/*****************************************************************************
 * NAND flash
 ****************************************************************************/
static struct resource kirkwood_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KIRKWOOD_NAND_MEM_PHYS_BASE,
	.end		= KIRKWOOD_NAND_MEM_PHYS_BASE +
				KIRKWOOD_NAND_MEM_SIZE - 1,
};

static struct orion_nand_data kirkwood_nand_data = {
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
};

static struct platform_device kirkwood_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &kirkwood_nand_data,
	},
	.resource	= &kirkwood_nand_resource,
	.num_resources	= 1,
};

void __init kirkwood_nand_init(struct mtd_partition *parts, int nr_parts,
			       int chip_delay)
{
	kirkwood_nand_data.parts = parts;
	kirkwood_nand_data.nr_parts = nr_parts;
	kirkwood_nand_data.chip_delay = chip_delay;
	platform_device_register(&kirkwood_nand_flash);
}

void __init kirkwood_nand_init_rnb(struct mtd_partition *parts, int nr_parts,
				   int (*dev_ready)(struct mtd_info *))
{
	kirkwood_nand_data.parts = parts;
	kirkwood_nand_data.nr_parts = nr_parts;
	kirkwood_nand_data.dev_ready = dev_ready;
	platform_device_register(&kirkwood_nand_flash);
}

/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static void __init kirkwood_rtc_init(void)
{
	orion_rtc_init(RTC_PHYS_BASE, IRQ_KIRKWOOD_RTC);
}


/*****************************************************************************
 * SATA
 ****************************************************************************/
void __init kirkwood_sata_init(struct mv_sata_platform_data *sata_data)
{
	orion_sata_init(sata_data, SATA_PHYS_BASE, IRQ_KIRKWOOD_SATA);
}


/*****************************************************************************
 * SD/SDIO/MMC
 ****************************************************************************/
static struct resource mvsdio_resources[] = {
	[0] = {
		.start	= SDIO_PHYS_BASE,
		.end	= SDIO_PHYS_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_KIRKWOOD_SDIO,
		.end	= IRQ_KIRKWOOD_SDIO,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 mvsdio_dmamask = DMA_BIT_MASK(32);

static struct platform_device kirkwood_sdio = {
	.name		= "mvsdio",
	.id		= -1,
	.dev		= {
		.dma_mask = &mvsdio_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(mvsdio_resources),
	.resource	= mvsdio_resources,
};

void __init kirkwood_sdio_init(struct mvsdio_platform_data *mvsdio_data)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	if (rev == 0 && dev != MV88F6282_DEV_ID) /* catch all Kirkwood Z0's */
		mvsdio_data->clock = 100000000;
	else
		mvsdio_data->clock = 200000000;
	kirkwood_sdio.dev.platform_data = mvsdio_data;
	platform_device_register(&kirkwood_sdio);
}


/*****************************************************************************
 * SPI
 ****************************************************************************/
void __init kirkwood_spi_init(void)
{
	orion_spi_init(SPI_PHYS_BASE);
}


/*****************************************************************************
 * I2C
 ****************************************************************************/
void __init kirkwood_i2c_init(void)
{
	orion_i2c_init(I2C_PHYS_BASE, IRQ_KIRKWOOD_TWSI, 8);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/

void __init kirkwood_uart0_init(void)
{
	orion_uart0_init(UART0_VIRT_BASE, UART0_PHYS_BASE,
			 IRQ_KIRKWOOD_UART_0, tclk);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init kirkwood_uart1_init(void)
{
	orion_uart1_init(UART1_VIRT_BASE, UART1_PHYS_BASE,
			 IRQ_KIRKWOOD_UART_1, tclk);
}

/*****************************************************************************
 * Cryptographic Engines and Security Accelerator (CESA)
 ****************************************************************************/
void __init kirkwood_crypto_init(void)
{
	orion_crypto_init(CRYPTO_PHYS_BASE, KIRKWOOD_SRAM_PHYS_BASE,
			  KIRKWOOD_SRAM_SIZE, IRQ_KIRKWOOD_CRYPTO);
}


/*****************************************************************************
 * XOR0
 ****************************************************************************/
void __init kirkwood_xor0_init(void)
{
	orion_xor0_init(XOR0_PHYS_BASE, XOR0_HIGH_PHYS_BASE,
			IRQ_KIRKWOOD_XOR_00, IRQ_KIRKWOOD_XOR_01);
}


/*****************************************************************************
 * XOR1
 ****************************************************************************/
void __init kirkwood_xor1_init(void)
{
	orion_xor1_init(XOR1_PHYS_BASE, XOR1_HIGH_PHYS_BASE,
			IRQ_KIRKWOOD_XOR_10, IRQ_KIRKWOOD_XOR_11);
}


/*****************************************************************************
 * Watchdog
 ****************************************************************************/
void __init kirkwood_wdt_init(void)
{
	orion_wdt_init();
}

/*****************************************************************************
 * CPU idle
 ****************************************************************************/
static struct resource kirkwood_cpuidle_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
		.start	= DDR_OPERATION_BASE,
		.end	= DDR_OPERATION_BASE + 3,
	},
};

static struct platform_device kirkwood_cpuidle = {
	.name		= "kirkwood_cpuidle",
	.id		= -1,
	.resource	= kirkwood_cpuidle_resource,
	.num_resources	= 1,
};

void __init kirkwood_cpuidle_init(void)
{
	platform_device_register(&kirkwood_cpuidle);
}

/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init kirkwood_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
}

int kirkwood_tclk;

static int __init kirkwood_find_tclk(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if (dev == MV88F6281_DEV_ID || dev == MV88F6282_DEV_ID)
		if (((readl(SAMPLE_AT_RESET) >> 21) & 1) == 0)
			return 200000000;

	return 166666667;
}

void __init kirkwood_timer_init(void)
{
	kirkwood_tclk = kirkwood_find_tclk();

	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_KIRKWOOD_BRIDGE, kirkwood_tclk);
}

/*****************************************************************************
 * Audio
 ****************************************************************************/
static struct resource kirkwood_audio_resources[] = {
	[0] = {
		.start  = AUDIO_PHYS_BASE,
		.end    = AUDIO_PHYS_BASE + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_KIRKWOOD_I2S,
		.end    = IRQ_KIRKWOOD_I2S,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct kirkwood_asoc_platform_data kirkwood_audio_data = {
	.burst       = 128,
};

static struct platform_device kirkwood_audio_device = {
	.name		= "mvebu-audio",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(kirkwood_audio_resources),
	.resource	= kirkwood_audio_resources,
	.dev		= {
		.platform_data	= &kirkwood_audio_data,
	},
};

void __init kirkwood_audio_init(void)
{
	platform_device_register(&kirkwood_audio_device);
}

/*****************************************************************************
 * CPU Frequency
 ****************************************************************************/
static struct resource kirkwood_cpufreq_resources[] = {
	[0] = {
		.start  = CPU_CONTROL_PHYS,
		.end    = CPU_CONTROL_PHYS + 3,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_cpufreq_device = {
	.name		= "kirkwood-cpufreq",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(kirkwood_cpufreq_resources),
	.resource	= kirkwood_cpufreq_resources,
};

void __init kirkwood_cpufreq_init(void)
{
	platform_device_register(&kirkwood_cpufreq_device);
}

/*****************************************************************************
 * General
 ****************************************************************************/
/*
 * Identify device ID and revision.
 */
char * __init kirkwood_id(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if (dev == MV88F6281_DEV_ID) {
		if (rev == MV88F6281_REV_Z0)
			return "MV88F6281-Z0";
		else if (rev == MV88F6281_REV_A0)
			return "MV88F6281-A0";
		else if (rev == MV88F6281_REV_A1)
			return "MV88F6281-A1";
		else
			return "MV88F6281-Rev-Unsupported";
	} else if (dev == MV88F6192_DEV_ID) {
		if (rev == MV88F6192_REV_Z0)
			return "MV88F6192-Z0";
		else if (rev == MV88F6192_REV_A0)
			return "MV88F6192-A0";
		else if (rev == MV88F6192_REV_A1)
			return "MV88F6192-A1";
		else
			return "MV88F6192-Rev-Unsupported";
	} else if (dev == MV88F6180_DEV_ID) {
		if (rev == MV88F6180_REV_A0)
			return "MV88F6180-Rev-A0";
		else if (rev == MV88F6180_REV_A1)
			return "MV88F6180-Rev-A1";
		else
			return "MV88F6180-Rev-Unsupported";
	} else if (dev == MV88F6282_DEV_ID) {
		if (rev == MV88F6282_REV_A0)
			return "MV88F6282-Rev-A0";
		else if (rev == MV88F6282_REV_A1)
			return "MV88F6282-Rev-A1";
		else
			return "MV88F6282-Rev-Unsupported";
	} else {
		return "Device-Unknown";
	}
}

void __init kirkwood_setup_wins(void)
{
	mvebu_mbus_add_window_by_id(KIRKWOOD_MBUS_NAND_TARGET,
				    KIRKWOOD_MBUS_NAND_ATTR,
				    KIRKWOOD_NAND_MEM_PHYS_BASE,
				    KIRKWOOD_NAND_MEM_SIZE);
	mvebu_mbus_add_window_by_id(KIRKWOOD_MBUS_SRAM_TARGET,
				    KIRKWOOD_MBUS_SRAM_ATTR,
				    KIRKWOOD_SRAM_PHYS_BASE,
				    KIRKWOOD_SRAM_SIZE);
}

void __init kirkwood_l2_init(void)
{
#ifdef CONFIG_CACHE_FEROCEON_L2
#ifdef CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH
	writel(readl(L2_CONFIG_REG) | L2_WRITETHROUGH, L2_CONFIG_REG);
	feroceon_l2_init(1);
#else
	writel(readl(L2_CONFIG_REG) & ~L2_WRITETHROUGH, L2_CONFIG_REG);
	feroceon_l2_init(0);
#endif
#endif
}

void __init kirkwood_init(void)
{
	pr_info("Kirkwood: %s, TCLK=%d.\n", kirkwood_id(), kirkwood_tclk);

	/*
	 * Disable propagation of mbus errors to the CPU local bus,
	 * as this causes mbus errors (which can occur for example
	 * for PCI aborts) to throw CPU aborts, which we're not set
	 * up to deal with.
	 */
	writel(readl(CPU_CONFIG) & ~CPU_CONFIG_ERROR_PROP, CPU_CONFIG);

	BUG_ON(mvebu_mbus_init("marvell,kirkwood-mbus",
			BRIDGE_WINS_BASE, BRIDGE_WINS_SZ,
			DDR_WINDOW_CPU_BASE, DDR_WINDOW_CPU_SZ));

	kirkwood_setup_wins();

	kirkwood_l2_init();

	/* Setup root of clk tree */
	kirkwood_clk_init();

	/* internal devices that every board has */
	kirkwood_rtc_init();
	kirkwood_wdt_init();
	kirkwood_xor0_init();
	kirkwood_xor1_init();
	kirkwood_crypto_init();

	kirkwood_pm_init();
	kirkwood_cpuidle_init();
#ifdef CONFIG_KEXEC
	kexec_reinit = kirkwood_enable_pcie;
#endif
}

void kirkwood_restart(enum reboot_mode mode, const char *cmd)
{
	/*
	 * Enable soft reset to assert RSTOUTn.
	 */
	writel(SOFT_RESET_OUT_EN, RSTOUTn_MASK);

	/*
	 * Assert soft reset.
	 */
	writel(SOFT_RESET, SYSTEM_SOFT_RESET);

	while (1)
		;
}

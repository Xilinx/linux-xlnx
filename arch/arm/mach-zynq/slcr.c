/*
 * Xilinx SLCR driver
 *
 * Copyright (c) 2011 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/export.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <mach/slcr.h>

#define DRIVER_NAME "xslcr"

#define XSLCR_LOCK			0x4   /* SLCR lock register */
#define XSLCR_UNLOCK			0x8   /* SCLR unlock register */
#define XSLCR_APER_CLK_CTRL_OFFSET	0x12C /* AMBA Peripheral Clk Control */
#define XSLCR_USB0_CLK_CTRL_OFFSET	0x130 /* USB 0 ULPI Clock Control */
#define XSLCR_USB1_CLK_CTRL_OFFSET	0x134 /* USB 1 ULPI Clock Control */
#define XSLCR_EMAC0_RCLK_CTRL_OFFSET	0x138 /* EMAC0 RX Clock Control */
#define XSLCR_EMAC1_RCLK_CTRL_OFFSET	0x13C /* EMAC1 RX Clock Control */
#define XSLCR_EMAC0_CLK_CTRL_OFFSET	0x140 /* EMAC0 Reference Clk Control */
#define XSLCR_EMAC1_CLK_CTRL_OFFSET	0x144 /* EMAC1 Reference Clk Control */
#define XSLCR_SMC_CLK_CTRL_OFFSET	0x148 /* SMC Reference Clock Control */
#define XSLCR_QSPI_CLK_CTRL_OFFSET	0x14C /* QSPI Reference Clock Control */
#define XSLCR_SDIO_CLK_CTRL_OFFSET	0x150 /* SDIO Reference Clock Control */
#define XSLCR_UART_CLK_CTRL_OFFSET	0x154 /* UART Reference Clock Control */
#define XSLCR_SPI_CLK_CTRL_OFFSET	0x158 /* SPI Reference Clock Control */
#define XSLCR_CAN_CLK_CTRL_OFFSET	0x15C /* CAN Reference Clock Control */
#define XSLCR_PSS_RST_CTRL_OFFSET	0x200 /* PSS Software Reset Control */
#define XSLCR_DDR_RST_CTRL_OFFSET	0x204 /* DDR Software Reset Control */
#define XSLCR_AMBA_RST_CTRL_OFFSET	0x208 /* AMBA Software Reset Control */
#define XSLCR_DMAC_RST_CTRL_OFFSET	0x20C /* DMAC Software Reset Control */
#define XSLCR_USB_RST_CTRL_OFFSET	0x210 /* USB Software Reset Control */
#define XSLCR_EMAC_RST_CTRL_OFFSET	0x214 /* EMAC Software Reset Control */
#define XSLCR_SDIO_RST_CTRL_OFFSET	0x218 /* SDIO Software Reset Control */
#define XSLCR_SPI_RST_CTRL_OFFSET	0x21C /* SPI Software Reset Control */
#define XSLCR_CAN_RST_CTRL_OFFSET	0x220 /* CAN Software Reset Control */
#define XSLCR_I2C_RST_CTRL_OFFSET	0x224 /* I2C Software Reset Control */
#define XSLCR_UART_RST_CTRL_OFFSET	0x228 /* UART Software Reset Control */
#define XSLCR_GPIO_RST_CTRL_OFFSET	0x22C /* GPIO Software Reset Control */
#define XSLCR_QSPI_RST_CTRL_OFFSET	0x230 /* QSpI Software Reset Control */
#define XSLCR_SMC_RST_CTRL_OFFSET	0x234 /* SMC Software Reset Control */
#define XSLCR_OCM_RST_CTRL_OFFSET	0x238 /* OCM Software Reset Control */
#define XSLCR_DEVC_RST_CTRL_OFFSET	0x23C /* Dev Cfg SW Reset Control */
#define XSLCR_FPGA_RST_CTRL_OFFSET	0x240 /* FPGA Software Reset Control */
#define XSLCR_A9_CPU_RST_CTRL		0x244 /* CPU Software Reset Control */
#define XSLCR_REBOOT_STATUS		0x258 /* PS Reboot Status */
#define XSLCR_MIO_PIN_00_OFFSET		0x700 /* MIO PIN0 control register */
#define XSLCR_LVL_SHFTR_EN_OFFSET	0x900 /* Level Shifters Enable */

/* Bit masks for AMBA Peripheral Clock Control register */
#define XSLCR_APER_CLK_CTRL_DMA0_MASK	0x00000001 /* DMA0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_USB0_MASK	0x00000004 /* USB0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_USB1_MASK	0x00000008 /* USB1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_EMAC0_MASK	0x00000040 /* EMAC0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_EMAC1_MASK	0x00000080 /* EMAC1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_SDI0_MASK	0x00000400 /* SDIO0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_SDI1_MASK	0x00000800 /* SDIO1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_SPI0_MASK	0x00004000 /* SPI0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_SPI1_MASK	0x00008000 /* SPI1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_CAN0_MASK	0x00010000 /* CAN0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_CAN1_MASK	0x00020000 /* CAN1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_I2C0_MASK	0x00040000 /* I2C0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_I2C1_MASK	0x00080000 /* I2C1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_UART0_MASK	0x00100000 /* UART0 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_UART1_MASK	0x00200000 /* UART1 AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_GPIO_MASK	0x00400000 /* GPIO AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_QSPI_MASK	0x00800000 /* QSPI AMBA Clock active */
#define XSLCR_APER_CLK_CTRL_SMC_MASK	0x01000000 /* SMC AMBA Clock active */

#define XSLCR_MIO_L0_SHIFT		1
#define XSLCR_MIO_L1_SHIFT		2
#define XSLCR_MIO_L2_SHIFT		3
#define XSLCR_MIO_L3_SHIFT		5

#define XSLCR_MIO_LMASK			0x000000FE

#define XSLCR_MIO_PIN_XX_TRI_ENABLE	0x00000001

/* The following constants define L0 Mux Peripheral Enables */
#define XSLCR_MIO_PIN_EMAC_ENABLE	(0x01 << XSLCR_MIO_L0_SHIFT)
#define XSLCR_MIO_PIN_QSPI_ENABLE	(0x01 << XSLCR_MIO_L0_SHIFT)

/* The following constants define L1 Mux Enables */
#define XSLCR_MIO_PIN_USB_ENABLE	(0x01 << XSLCR_MIO_L1_SHIFT)
#define XSLCR_MIO_PIN_TRACE_PORT_ENABLE	(0x01 << XSLCR_MIO_L1_SHIFT)

/* The following constants define L2 Mux Peripheral Enables */
#define XSLCR_MIO_PIN_SRAM_NOR_ENABLE	(0x01 << XSLCR_MIO_L2_SHIFT)
#define XSLCR_MIO_PIN_NAND_ENABLE	(0x02 << XSLCR_MIO_L2_SHIFT)

/* The following constants define L3 Mux Peripheral Enables */
#define XSLCR_MIO_PIN_GPIO_ENABLE	(0x00 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_CAN_ENABLE	(0x01 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_IIC_ENABLE	(0x02 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_WDT_ENABLE	(0x03 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_JTAG_ENABLE	(0x03 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_SDIO_ENABLE	(0x04 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_MDIO0_ENABLE	(0x04 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_MDIO1_ENABLE	(0x05 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_SPI_ENABLE	(0x05 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_TTC_ENABLE	(0x06 << XSLCR_MIO_L3_SHIFT)
#define XSLCR_MIO_PIN_UART_ENABLE	(0x07 << XSLCR_MIO_L3_SHIFT)

/* The following constants define the number of pins associated with each
 * peripheral */
#define XSLCR_MIO_NUM_EMAC_PINS		12
#define XSLCR_MIO_NUM_USB_PINS		12
#define XSLCR_MIO_NUM_TRACE_DATA2_PINS	04
#define XSLCR_MIO_NUM_TRACE_DATA4_PINS	06
#define XSLCR_MIO_NUM_TRACE_DATA8_PINS	10
#define XSLCR_MIO_NUM_TRACE_DATA16_PINS	18
#define XSLCR_MIO_NUM_NAND_PINS		(21+1)
#define XSLCR_MIO_NUM_SMC_A25_PINS	01
#define XSLCR_MIO_NUM_SMC_CS_PINS	01
#define XSLCR_MIO_NUM_NAND_CS_PINS	01
#define XSLCR_MIO_NUM_SRAM_NOR_PINS	38
#define XSLCR_MIO_NUM_QSPI_PINS		05
#define XSLCR_MIO_NUM_QSPI_SEL_PINS	01
#define XSLCR_MIO_NUM_QSPI_FOC_PINS	01
#define XSLCR_MIO_NUM_GPIO_PINS		01
#define XSLCR_MIO_NUM_CAN_PINS		02
#define XSLCR_MIO_NUM_IIC_PINS		02
#define XSLCR_MIO_NUM_JTAG_PINS		04
#define XSLCR_MIO_NUM_WDT_PINS		02
#define XSLCR_MIO_NUM_MDIO_PINS		02
#define XSLCR_MIO_NUM_SDIO_PINS		06
#define XSLCR_MIO_NUM_SPI_PINS		06
#define XSLCR_MIO_NUM_TTC_PINS		02
#define XSLCR_MIO_NUM_UART_PINS		02

/* The following two constants define the indices of the MIO peripherals EMAC0/1
 * in the array mio_periph_name */
#define MIO_EMAC0			0
#define MIO_EMAC1			1

#define XSLCR_MDIO_PIN_0		52
#define XSLCR_MIO_MAX_PIN		54

#define xslcr_writereg(offset, val)	__raw_writel(val, offset)
#define xslcr_readreg(offset)		__raw_readl(offset)

/**
 * struct xslcr - slcr device data.
 * @regs:	baseaddress of device.
 * @io_lock:	spinlock used for synchronization.
 *
 */
struct xslcr {
	void __iomem	*regs;
	spinlock_t	io_lock;
};

static struct xslcr *slcr;

/**
 * xslcr_mio - Holds information required to enable/disable a MIO peripheral.
 *
 * @set_pins:	Pointer to array of first pins in each pin set for this periph
 * @max_sets:	Max pin sets for this periph
 * @numpins:	Number of pins for this periph
 * @enable_val:	Enable value to assign a MIO pin to this periph
 * @amba_clk_mask:	AMBA peripheral clock enable mask for this periph
 * @periph_clk_reg:	Clock enable register offset for the periph
 * @periph_clk_mask:	Clock enable mask for the periph
 */
struct xslcr_mio {
	const int *set_pins;
	int max_sets;
	int numpins;
	u32 enable_val;
	u32 amba_clk_mask;
	u32 periph_clk_reg;
	u32 periph_clk_mask;
};

/**
 * xslcr_periph_reset - Holds information required to reset a peripheral.
 *
 * @reg_offset:	offset of the reset reg for the peripheral
 * @reset_mask:	mask to reset the peripheral
 */
struct xslcr_periph_reset {
	u32 reg_offset;
	u32 reset_mask;
};

/* MIO peripheral names */
static const char * mio_periph_name[] = {
	"emac0",
	"emac1",
	"qspi0",
	"qspi0_sel",
	"qspi1",
	"qspi1_sel",
	"qspi_foc",
	"trace_data2",
	"trace_data4",
	"trace_data8",
	"trace_data16",
	"usb0",
	"usb1",
	"smc_a25",
	"smc_cs",
	"sram_nor",
	"nand",
	"nand_cs",
	"gpio00",
	"gpio01",
	"gpio02",
	"gpio03",
	"gpio04",
	"gpio05",
	"gpio06",
	"gpio07",
	"gpio08",
	"gpio09",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
	"gpio16",
	"gpio17",
	"gpio18",
	"gpio19",
	"gpio20",
	"gpio21",
	"gpio22",
	"gpio23",
	"gpio24",
	"gpio25",
	"gpio26",
	"gpio27",
	"gpio28",
	"gpio29",
	"gpio30",
	"gpio31",
	"gpio32",
	"gpio33",
	"gpio34",
	"gpio35",
	"gpio36",
	"gpio37",
	"gpio38",
	"gpio39",
	"gpio40",
	"gpio41",
	"gpio42",
	"gpio43",
	"gpio44",
	"gpio45",
	"gpio46",
	"gpio47",
	"gpio48",
	"gpio49",
	"gpio50",
	"gpio51",
	"gpio52",
	"gpio53",
	"can0",
	"can1",
	"iic0",
	"iic1",
	"jtag",
	"wdt",
	"mdio0",
	"sdio0",
	"sdio1",
	"mdio1",
	"spi0",
	"spi1",
	"ttc0",
	"ttc1",
	"uart0",
	"uart1",
};

/* Each bit in this array is a flag that indicates whether a mio peripheral
 * is assigned. The order of bits in this array is same as the order of
 * peripheral names in the array mio_periph_name */
static u32 periph_status[2] = {0, 0};

/* Each element in the following array holds the active pinset of a MIO
 * peripheral. The order of peripherals in this array is same as the order of
 * peripheral names in the array mio_periph_name */
static u32 active_pinset[ARRAY_SIZE(mio_periph_name)];

/*
 * The following arrays contain the first pin in each pin set of a MIO
 * corresponding peripheral.
 */
static const int emac0_pins[] = {
	16
};

static const int emac1_pins[] = {
	28, 40
};

static const int qspi0_pins[] = {
	2
};

static const int qspi0_sel_pins[] = {
	1
};

static const int qspi1_pins[] = {
	9
};

static const int qspi1_sel_pins[] = {
	0
};

static const int qspi_foc_pins[] = {
	8
};

static const int trace_data2_pins[] = {
	12, 24
};

static const int trace_data4_pins[] = {
	10, 22
};

static const int trace_data8_pins[] = {
	10
};

static const int trace_data16_pins[] = {
	2
};

static const int usb0_pins[] = {
	28
};

static const int usb1_pins[] = {
	40
};

static const int smc_a25_pins[] = {
	1
};

static const int smc_cs_pins[] = {
	0, 1
};

static const int sram_nor_pins[] = {
	2
};

static const int nand_pins[] = {
	2
};

static const int nand_cs_pins[] = {
	0
};

static const int gpio00_pins[] = {
	0
};

static const int gpio01_pins[] = {
	1
};


static const int gpio02_pins[] = {
	2
};

static const int gpio03_pins[] = {
	3
};

static const int gpio04_pins[] = {
	4
};

static const int gpio05_pins[] = {
	5
};

static const int gpio06_pins[] = {
	6
};

static const int gpio07_pins[] = {
	7
};


static const int gpio08_pins[] = {
	8
};

static const int gpio09_pins[] = {
	9
};


static const int gpio10_pins[] = {
	10
};

static const int gpio11_pins[] = {
	11
};

static const int gpio12_pins[] = {
	12
};

static const int gpio13_pins[] = {
	13
};

static const int gpio14_pins[] = {
	14
};


static const int gpio15_pins[] = {
	15
};

static const int gpio16_pins[] = {
	16
};

static const int gpio17_pins[] = {
	17
};

static const int gpio18_pins[] = {
	18
};

static const int gpio19_pins[] = {
	19
};

static const int gpio20_pins[] = {
	20
};

static const int gpio21_pins[] = {
	21
};


static const int gpio22_pins[] = {
	22
};

static const int gpio23_pins[] = {
	23
};

static const int gpio24_pins[] = {
	24
};

static const int gpio25_pins[] = {
	25
};

static const int gpio26_pins[] = {
	26
};

static const int gpio27_pins[] = {
	27
};


static const int gpio28_pins[] = {
	28
};

static const int gpio29_pins[] = {
	29
};


static const int gpio30_pins[] = {
	30
};

static const int gpio31_pins[] = {
	31
};

static const int gpio32_pins[] = {
	32
};

static const int gpio33_pins[] = {
	33
};

static const int gpio34_pins[] = {
	34
};


static const int gpio35_pins[] = {
	35
};

static const int gpio36_pins[] = {
	36
};

static const int gpio37_pins[] = {
	37
};

static const int gpio38_pins[] = {
	38
};

static const int gpio39_pins[] = {
	39
};

static const int gpio40_pins[] = {
	40
};

static const int gpio41_pins[] = {
	41
};

static const int gpio42_pins[] = {
	42
};

static const int gpio43_pins[] = {
	43
};

static const int gpio44_pins[] = {
	44
};


static const int gpio45_pins[] = {
	45
};

static const int gpio46_pins[] = {
	46
};

static const int gpio47_pins[] = {
	47
};

static const int gpio48_pins[] = {
	48
};

static const int gpio49_pins[] = {
	49
};

static const int gpio50_pins[] = {
	50
};

static const int gpio51_pins[] = {
	51
};

static const int gpio52_pins[] = {
	52
};

static const int gpio53_pins[] = {
	53
};

static const int can0_pins[] = {
	10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50
};

static const int can1_pins[] = {
	8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52
};

static const int iic0_pins[] = {
	10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50
};

static const int iic1_pins[] = {
	8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52
};

static const int jtag0_pins[] = {
	10, 22, 34, 46
};

static const int wdt_pins[] = {
	14, 26, 38, 50, 52
};

static const int mdio0_pins[] = {
	52
};

static const int sdio0_pins[] = {
	16, 28, 40
};

static const int sdio1_pins[] = {
	10, 22, 34, 46
};

static const int mdio1_pins[] = {
	52
};

static const int spi0_pins[] = {
	16, 28, 40
};

static const int spi1_pins[] = {
	10, 22, 34, 46
};

static const int ttc0_pins[] = {
	18, 30, 42
};

static const int ttc1_pins[] = {
	16, 28, 40
};

static const int uart0_pins[] = {
	10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50
};

static const int uart1_pins[] = {
	8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52
};

/* The following array contains required info for enabling MIO peripherals and
 * their clocks. The order of the structures in this array is same as the order
 * of peripheral names in the array mio_periph_name */
static const struct xslcr_mio mio_periphs[] = {
	{
		emac0_pins,
		ARRAY_SIZE(emac0_pins),
		XSLCR_MIO_NUM_EMAC_PINS,
		XSLCR_MIO_PIN_EMAC_ENABLE,
		XSLCR_APER_CLK_CTRL_EMAC0_MASK,
		XSLCR_EMAC0_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		emac1_pins,
		ARRAY_SIZE(emac1_pins),
		XSLCR_MIO_NUM_EMAC_PINS,
		XSLCR_MIO_PIN_EMAC_ENABLE,
		XSLCR_APER_CLK_CTRL_EMAC1_MASK,
		XSLCR_EMAC1_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		qspi0_pins,
		ARRAY_SIZE(qspi0_pins),
		XSLCR_MIO_NUM_QSPI_PINS,
		XSLCR_MIO_PIN_QSPI_ENABLE,
		XSLCR_APER_CLK_CTRL_QSPI_MASK,
		XSLCR_QSPI_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		qspi0_sel_pins,
		ARRAY_SIZE(qspi0_sel_pins),
		XSLCR_MIO_NUM_QSPI_SEL_PINS,
		XSLCR_MIO_PIN_QSPI_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		qspi1_pins,
		ARRAY_SIZE(qspi1_pins),
		XSLCR_MIO_NUM_QSPI_PINS,
		XSLCR_MIO_PIN_QSPI_ENABLE,
		XSLCR_APER_CLK_CTRL_QSPI_MASK,
		XSLCR_QSPI_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		qspi1_sel_pins,
		ARRAY_SIZE(qspi1_sel_pins),
		XSLCR_MIO_NUM_QSPI_SEL_PINS,
		XSLCR_MIO_PIN_QSPI_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{	qspi_foc_pins,
		ARRAY_SIZE(qspi_foc_pins),
		XSLCR_MIO_NUM_QSPI_FOC_PINS,
		XSLCR_MIO_PIN_QSPI_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		trace_data2_pins,
		ARRAY_SIZE(trace_data2_pins),
		XSLCR_MIO_NUM_TRACE_DATA2_PINS,
		XSLCR_MIO_PIN_TRACE_PORT_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		trace_data4_pins,
		ARRAY_SIZE(trace_data4_pins),
		XSLCR_MIO_NUM_TRACE_DATA4_PINS,
		XSLCR_MIO_PIN_TRACE_PORT_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		trace_data8_pins,
		ARRAY_SIZE(trace_data8_pins),
		XSLCR_MIO_NUM_TRACE_DATA8_PINS,
		XSLCR_MIO_PIN_TRACE_PORT_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		trace_data16_pins,
		ARRAY_SIZE(trace_data16_pins),
		XSLCR_MIO_NUM_TRACE_DATA4_PINS,
		XSLCR_MIO_PIN_TRACE_PORT_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		usb0_pins,
		ARRAY_SIZE(usb0_pins),
		XSLCR_MIO_NUM_USB_PINS,
		XSLCR_MIO_PIN_USB_ENABLE,
		XSLCR_APER_CLK_CTRL_USB0_MASK,
		XSLCR_USB0_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		usb1_pins,
		ARRAY_SIZE(usb1_pins),
		XSLCR_MIO_NUM_USB_PINS,
		XSLCR_MIO_PIN_USB_ENABLE,
		XSLCR_APER_CLK_CTRL_USB1_MASK,
		XSLCR_USB1_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		smc_a25_pins,
		ARRAY_SIZE(smc_a25_pins),
		XSLCR_MIO_NUM_SMC_A25_PINS,
		XSLCR_MIO_PIN_SRAM_NOR_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		smc_cs_pins,
		ARRAY_SIZE(smc_cs_pins),
		XSLCR_MIO_NUM_SMC_CS_PINS,
		XSLCR_MIO_PIN_SRAM_NOR_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		sram_nor_pins,
		ARRAY_SIZE(sram_nor_pins),
		XSLCR_MIO_NUM_SRAM_NOR_PINS,
		XSLCR_MIO_PIN_SRAM_NOR_ENABLE,
		XSLCR_APER_CLK_CTRL_SMC_MASK,
		XSLCR_SMC_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		nand_pins,
		ARRAY_SIZE(nand_pins),
		XSLCR_MIO_NUM_NAND_PINS,
		XSLCR_MIO_PIN_NAND_ENABLE,
		XSLCR_APER_CLK_CTRL_SMC_MASK,
		XSLCR_SMC_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		nand_cs_pins,
		ARRAY_SIZE(nand_cs_pins),
		XSLCR_MIO_NUM_NAND_CS_PINS,
		XSLCR_MIO_PIN_NAND_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		gpio00_pins,
		ARRAY_SIZE(gpio00_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio01_pins,
		ARRAY_SIZE(gpio01_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio02_pins,
		ARRAY_SIZE(gpio02_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio03_pins,
		ARRAY_SIZE(gpio03_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio04_pins,
		ARRAY_SIZE(gpio04_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio05_pins,
		ARRAY_SIZE(gpio05_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio06_pins,
		ARRAY_SIZE(gpio06_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio07_pins,
		ARRAY_SIZE(gpio07_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio08_pins,
		ARRAY_SIZE(gpio08_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio09_pins,
		ARRAY_SIZE(gpio09_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio10_pins,
		ARRAY_SIZE(gpio10_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio11_pins,
		ARRAY_SIZE(gpio11_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio12_pins,
		ARRAY_SIZE(gpio12_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio13_pins,
		ARRAY_SIZE(gpio13_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio14_pins,
		ARRAY_SIZE(gpio14_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio15_pins,
		ARRAY_SIZE(gpio15_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio16_pins,
		ARRAY_SIZE(gpio16_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio17_pins,
		ARRAY_SIZE(gpio17_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio18_pins,
		ARRAY_SIZE(gpio18_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio19_pins,
		ARRAY_SIZE(gpio19_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio20_pins,
		ARRAY_SIZE(gpio20_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio21_pins,
		ARRAY_SIZE(gpio21_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio22_pins,
		ARRAY_SIZE(gpio22_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio23_pins,
		ARRAY_SIZE(gpio23_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio24_pins,
		ARRAY_SIZE(gpio24_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio25_pins,
		ARRAY_SIZE(gpio25_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio26_pins,
		ARRAY_SIZE(gpio26_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio27_pins,
		ARRAY_SIZE(gpio27_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio28_pins,
		ARRAY_SIZE(gpio28_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio29_pins,
		ARRAY_SIZE(gpio29_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio30_pins,
		ARRAY_SIZE(gpio30_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio31_pins,
		ARRAY_SIZE(gpio31_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio32_pins,
		ARRAY_SIZE(gpio32_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio33_pins,
		ARRAY_SIZE(gpio33_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio34_pins,
		ARRAY_SIZE(gpio34_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio35_pins,
		ARRAY_SIZE(gpio35_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio36_pins,
		ARRAY_SIZE(gpio36_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio37_pins,
		ARRAY_SIZE(gpio37_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio38_pins,
		ARRAY_SIZE(gpio38_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio39_pins,
		ARRAY_SIZE(gpio39_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio40_pins,
		ARRAY_SIZE(gpio40_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio41_pins,
		ARRAY_SIZE(gpio41_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio42_pins,
		ARRAY_SIZE(gpio42_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio43_pins,
		ARRAY_SIZE(gpio43_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio44_pins,
		ARRAY_SIZE(gpio44_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio45_pins,
		ARRAY_SIZE(gpio45_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio46_pins,
		ARRAY_SIZE(gpio46_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio47_pins,
		ARRAY_SIZE(gpio47_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio48_pins,
		ARRAY_SIZE(gpio48_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio49_pins,
		ARRAY_SIZE(gpio49_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio50_pins,
		ARRAY_SIZE(gpio50_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio51_pins,
		ARRAY_SIZE(gpio51_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio52_pins,
		ARRAY_SIZE(gpio52_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		gpio53_pins,
		ARRAY_SIZE(gpio53_pins),
		XSLCR_MIO_NUM_GPIO_PINS,
		XSLCR_MIO_PIN_GPIO_ENABLE,
		XSLCR_APER_CLK_CTRL_GPIO_MASK,
		0x00,
		0x00,
	},
	{
		can0_pins,
		ARRAY_SIZE(can0_pins),
		XSLCR_MIO_NUM_CAN_PINS,
		XSLCR_MIO_PIN_CAN_ENABLE,
		XSLCR_APER_CLK_CTRL_CAN0_MASK,
		XSLCR_CAN_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		can1_pins,
		ARRAY_SIZE(can1_pins),
		XSLCR_MIO_NUM_CAN_PINS,
		XSLCR_MIO_PIN_CAN_ENABLE,
		XSLCR_APER_CLK_CTRL_CAN1_MASK,
		XSLCR_CAN_CLK_CTRL_OFFSET,
		0x02,
	},
	{
		iic0_pins,
		ARRAY_SIZE(iic0_pins),
		XSLCR_MIO_NUM_IIC_PINS,
		XSLCR_MIO_PIN_IIC_ENABLE,
		XSLCR_APER_CLK_CTRL_I2C0_MASK,
		0x00,
		0x00,
	},
	{
		iic1_pins,
		ARRAY_SIZE(iic1_pins),
		XSLCR_MIO_NUM_IIC_PINS,
		XSLCR_MIO_PIN_IIC_ENABLE,
		XSLCR_APER_CLK_CTRL_I2C1_MASK,
		0x00,
		0x00,
	},
	{
		jtag0_pins,
		ARRAY_SIZE(jtag0_pins),
		XSLCR_MIO_NUM_JTAG_PINS,
		XSLCR_MIO_PIN_JTAG_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		wdt_pins,
		ARRAY_SIZE(wdt_pins),
		XSLCR_MIO_NUM_WDT_PINS,
		XSLCR_MIO_PIN_WDT_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		mdio0_pins,
		ARRAY_SIZE(mdio0_pins),
		XSLCR_MIO_NUM_MDIO_PINS,
		XSLCR_MIO_PIN_MDIO0_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		sdio0_pins,
		ARRAY_SIZE(sdio0_pins),
		XSLCR_MIO_NUM_SDIO_PINS,
		XSLCR_MIO_PIN_SDIO_ENABLE,
		XSLCR_APER_CLK_CTRL_SDI0_MASK,
		XSLCR_SDIO_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		sdio1_pins,
		ARRAY_SIZE(sdio1_pins),
		XSLCR_MIO_NUM_SDIO_PINS,
		XSLCR_MIO_PIN_SDIO_ENABLE,
		XSLCR_APER_CLK_CTRL_SDI1_MASK,
		XSLCR_SDIO_CLK_CTRL_OFFSET,
		0x02,
	},
	{
		mdio1_pins,
		ARRAY_SIZE(mdio1_pins),
		XSLCR_MIO_NUM_MDIO_PINS,
		XSLCR_MIO_PIN_MDIO1_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		spi0_pins,
		ARRAY_SIZE(spi0_pins),
		XSLCR_MIO_NUM_SPI_PINS,
		XSLCR_MIO_PIN_SPI_ENABLE,
		XSLCR_APER_CLK_CTRL_SPI0_MASK,
		XSLCR_SPI_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		spi1_pins,
		ARRAY_SIZE(spi1_pins),
		XSLCR_MIO_NUM_SPI_PINS,
		XSLCR_MIO_PIN_SPI_ENABLE,
		XSLCR_APER_CLK_CTRL_SPI0_MASK,
		XSLCR_SPI_CLK_CTRL_OFFSET,
		0x02,
	},
	{
		ttc0_pins,
		ARRAY_SIZE(ttc0_pins),
		XSLCR_MIO_NUM_TTC_PINS,
		XSLCR_MIO_PIN_TTC_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		ttc1_pins,
		ARRAY_SIZE(ttc1_pins),
		XSLCR_MIO_NUM_TTC_PINS,
		XSLCR_MIO_PIN_TTC_ENABLE,
		0x00,
		0x00,
		0x00,
	},
	{
		uart0_pins,
		ARRAY_SIZE(uart0_pins),
		XSLCR_MIO_NUM_UART_PINS,
		XSLCR_MIO_PIN_UART_ENABLE,
		XSLCR_APER_CLK_CTRL_UART0_MASK,
		XSLCR_UART_CLK_CTRL_OFFSET,
		0x01,
	},
	{
		uart1_pins,
		ARRAY_SIZE(uart1_pins),
		XSLCR_MIO_NUM_UART_PINS,
		XSLCR_MIO_PIN_UART_ENABLE,
		XSLCR_APER_CLK_CTRL_UART1_MASK,
		XSLCR_UART_CLK_CTRL_OFFSET,
		0x02,
	},
};

/* Peripherals that can be reset thru SLCR */
static const char * reset_periph_name[] = {
	"pss",
	"ddr",
	"sw_amba",
	"dmac",
	"usb0_amba",
	"usb1_amba",
	"usb0_usb",
	"usb1_usb",
	"eth0_mac",
	"eth1_mac",
	"eth0_rx",
	"eth1_rx",
	"eth0_ref",
	"eth1_ref",
	"sdio0_amba",
	"sdio1_amba",
	"sdio0_ref",
	"sdio1_ref",
	"spi0_amba",
	"spi1_ambs",
	"spi0_ref",
	"spi1_ref",
	"can0_amba",
	"can1_amba",
	"can0_ref",
	"can1_ref",
	"iic0_amba",
	"iic1_amba",
	"uart0_amba",
	"uart1_amba",
	"gpio_amba",
	"qspi_amba",
	"qspi_ref",
	"smc_amba",
	"smc_ref",
	"ocm_amba",
	"pcap2x",
	"devc_amba",
	"fpga0_out",
	"fpga1_out",
	"fpga2_out",
	"fpga3_out",
	"fpga_dma0",
	"fpga_dma1",
	"fpga_dma2",
	"fpga_dma3",
	"fpga_fmsw0",
	"fpga_fmsw1",
	"fpga_fssw0",
	"fpga_fssw1",
	"fpga_axds0",
	"fpga_axds1",
	"fpga_axds2",
	"fpga_axds3",
	"fpga_acp",
};

/* The following array contains the reset control register offset and the reset
 * mask for all the peripherals. The order of the structures is same as the
 * order of peripheral names in the array reset_periph_name */
static const struct xslcr_periph_reset reset_info[] = {
	{ XSLCR_PSS_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_DDR_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_AMBA_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_DMAC_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_USB_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_USB_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_USB_RST_CTRL_OFFSET,  0x00000010 },
	{ XSLCR_USB_RST_CTRL_OFFSET,  0x00000020 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000010 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000020 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000040 },
	{ XSLCR_EMAC_RST_CTRL_OFFSET,  0x00000080 },
	{ XSLCR_SDIO_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_SDIO_RST_CTRL_OFFSET, 0x00000002 },
	{ XSLCR_SDIO_RST_CTRL_OFFSET, 0x00000010 },
	{ XSLCR_SDIO_RST_CTRL_OFFSET, 0x00000020 },
	{ XSLCR_SPI_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_SPI_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_SPI_RST_CTRL_OFFSET,  0x00000004 },
	{ XSLCR_SPI_RST_CTRL_OFFSET,  0x00000008 },
	{ XSLCR_CAN_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_CAN_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_CAN_RST_CTRL_OFFSET,  0x00000004 },
	{ XSLCR_CAN_RST_CTRL_OFFSET,  0x00000008 },
	{ XSLCR_I2C_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_I2C_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_UART_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_UART_RST_CTRL_OFFSET, 0x00000002 },
	{ XSLCR_GPIO_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_QSPI_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_QSPI_RST_CTRL_OFFSET, 0x00000002 },
	{ XSLCR_SMC_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_SMC_RST_CTRL_OFFSET,  0x00000002 },
	{ XSLCR_OCM_RST_CTRL_OFFSET,  0x00000001 },
	{ XSLCR_DEVC_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_DEVC_RST_CTRL_OFFSET, 0x00000002 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000001 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000002 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000004 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000008 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000100 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000200 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000400 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00000800 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00001000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00002000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00010000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00020000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00100000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00200000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00400000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x00800000 },
	{ XSLCR_FPGA_RST_CTRL_OFFSET, 0x01000000 },
};

/**
 * xslcr_system_reset - Reset the entire system.
 *
 **/
void xslcr_system_reset(void)
{
	u32 reboot;

	/* Unlock the SLCR then reset the system.
	 * Note that this seems to require raw i/o
	 * functions or there's a lockup?
	 */
	xslcr_writereg(slcr->regs + XSLCR_UNLOCK, 0xDF0D);

	/* Clear 0x0F000000 bits of reboot status register to workaround
	 * the FSBL not loading the bitstream after soft-reboot
	 * This is a temporary solution until we know more.
	 */
	reboot = xslcr_readreg(slcr->regs + XSLCR_REBOOT_STATUS);
	xslcr_writereg(slcr->regs + XSLCR_REBOOT_STATUS, reboot & 0xF0FFFFFF);
	xslcr_writereg(slcr->regs + XSLCR_PSS_RST_CTRL_OFFSET, 1);
}

/**
 * xslcr_write - Write to a register in SLCR block
 *
 * @offset:	Register offset in SLCR block
 * @val:	Value to write to the register
 **/
void xslcr_write(u32 offset, u32 val)
{
	xslcr_writereg(slcr->regs + offset, val);
}
EXPORT_SYMBOL(xslcr_write);

/**
 * xslcr_read - Read a register in SLCR block
 *
 * @offset:	Register offset in SLCR block
 *
 * return:	Value read from the SLCR register
 **/
u32 xslcr_read(u32 offset)
{
	return xslcr_readreg(slcr->regs + offset);
}
EXPORT_SYMBOL(xslcr_read);

/**
 * xslcr_init_preload_fpga - Disable communication from the PL to PS.
 */
void xslcr_init_preload_fpga(void) {

	/* Assert FPGA top level output resets */
	xslcr_write(XSLCR_FPGA_RST_CTRL_OFFSET, 0xF);

	/* Disable level shifters */
	xslcr_write(XSLCR_LVL_SHFTR_EN_OFFSET, 0x0);

	/* Enable output level shifters */
	xslcr_write(XSLCR_LVL_SHFTR_EN_OFFSET, 0xA);
}
EXPORT_SYMBOL(xslcr_init_preload_fpga);

/**
 * xslcr_init_postload_fpga - Re-enable communication from the PL to PS.
 */
void xslcr_init_postload_fpga(void) {

	/* Enable level shifters */
	xslcr_write(XSLCR_LVL_SHFTR_EN_OFFSET, 0xF);

	/* Deassert AXI interface resets */
	xslcr_write(XSLCR_FPGA_RST_CTRL_OFFSET, 0x0);
}
EXPORT_SYMBOL(xslcr_init_postload_fpga);

/**
 * xslcr_set_bit - Set a bit
 *
 * @data:	Address of the data in which a bit is to be set
 * @bit:	Bit number to set
 **/
static inline void xslcr_set_bit(u32 *data, unsigned int bit)
{
	unsigned long mask = 1UL << (bit & 31);

	*(data + (bit >> 5)) |= mask;
}

/**
 * xslcr_clear_bit - Clear a bit
 *
 * @data:	Address of the data in which a bit is to be cleared
 * @bit:	Bit number to clear
 **/
static inline void xslcr_clear_bit(u32 *data, unsigned int bit)
{
	unsigned long mask = 1UL << (bit & 31);

	*(data + (bit >> 5)) &= ~mask;
}

/**
 * xslcr_test_bit - Check if a bit is set
 *
 * @data:	Address of the data in which a bit is to be checked
 * @bit:	Bit number to check
 *
 * return:	True or false
 **/
static inline int xslcr_test_bit(u32 *data, unsigned int bit)
{
	unsigned long mask = 1UL << (bit & 31);

	return (*(data + (bit >> 5)) & mask) != 0;
}

/**
 * xslcr_mio_isavailable - Check if a MIO pin is available for assignment.
 *
 * @pin		MIO pin to be checked.
 *
 * return:	-EBUSY if the pin is in use.
 *		0 if the pin is not assigned.
 **/
static int xslcr_mio_isavailable(u32 pin)
{
	u32 reg;

	reg = xslcr_readreg(slcr->regs + XSLCR_MIO_PIN_00_OFFSET + (pin * 4));
	if (reg & XSLCR_MIO_PIN_XX_TRI_ENABLE)
		return 0;

	return -EBUSY;	/* pin is assigned */
}

/**
 * xslcr_enable_mio_clock - Enable the clocks for a MIO peripheral.
 *
 * @mio_periph	id used to look up the data needed to enable clocks for this
 *		peripheral.
 *
 * This function enables the AMBA clock and the peripheral clock for a
 * peripheral. It also enables Rx clocks in case of EMAC0/EMAC1.
 **/
static void xslcr_enable_mio_clock(int mio_periph)
{
	const struct xslcr_mio *mio_ptr;
	u32 clk_reg;

	mio_ptr = &mio_periphs[mio_periph];

	/* enable AMBA clock and peripheral clock */
	clk_reg = xslcr_readreg(slcr->regs + XSLCR_APER_CLK_CTRL_OFFSET);
	clk_reg |= mio_ptr->amba_clk_mask;
	xslcr_writereg((slcr->regs + XSLCR_APER_CLK_CTRL_OFFSET), clk_reg);

	clk_reg = xslcr_readreg(slcr->regs + mio_ptr->periph_clk_reg);
	clk_reg |= mio_ptr->periph_clk_mask;
	xslcr_writereg((slcr->regs + mio_ptr->periph_clk_reg), clk_reg);

	/* enable Rx clocks for EMAC0 and EMAC1 */
	if (mio_periph == MIO_EMAC0)
		xslcr_writereg((slcr->regs + XSLCR_EMAC0_RCLK_CTRL_OFFSET),
				0x01);
	else if (mio_periph == MIO_EMAC1)
		xslcr_writereg((slcr->regs + XSLCR_EMAC1_RCLK_CTRL_OFFSET),
				0x01);
}

/**
 * xslcr_disable_mio_clock - Disable the clocks for a MIO peripheral.
 *
 * @mio_periph	id used to look up the data needed to disable clocks for this
 *		peripheral.
 *
 * This function disables the AMBA clock and the peripheral clock for a
 * peripheral. It also disables Rx clocks in case of EMAC0/EMAC1.
 **/
static void xslcr_disable_mio_clock(int mio_periph)
{
	const struct xslcr_mio *mio_ptr;
	u32 clk_reg;

	mio_ptr = &mio_periphs[mio_periph];

	/* disable AMBA clock and peripheral clock */
	clk_reg = xslcr_readreg(slcr->regs + XSLCR_APER_CLK_CTRL_OFFSET);
	clk_reg &= ~(mio_ptr->amba_clk_mask);
	xslcr_writereg((slcr->regs + XSLCR_APER_CLK_CTRL_OFFSET), clk_reg);

	clk_reg = xslcr_readreg(slcr->regs + mio_ptr->periph_clk_reg);
	clk_reg &= ~(mio_ptr->periph_clk_mask);
	xslcr_writereg((slcr->regs + mio_ptr->periph_clk_reg), clk_reg);

	/* disable Rx clocks for EMAC0 and EMAC1 */
	if (mio_periph == MIO_EMAC0)
		xslcr_writereg((slcr->regs + XSLCR_EMAC0_RCLK_CTRL_OFFSET),
				0x00);
	else if (mio_periph == MIO_EMAC1)
		xslcr_writereg((slcr->regs + XSLCR_EMAC1_RCLK_CTRL_OFFSET),
				0x00);
}

/**
 * xslcr_enable_mio_peripheral - Enable a MIO peripheral.
 *
 * @mio:	id used to lookup the data needed to enable the peripheral.
 *
 * This function enables a MIO peripheral on a pinset previously set by the
 * user, thru sysfs attribute 'pinset'.
 *
 * @return	0 if the peripheral is enabled on the given pin set.
 *		negative error if the peripheral is already enabled, if an
 *		invalid pinset is specified, or if the pins are assigned to a
 *		different peripheral.
 **/
static int xslcr_enable_mio_peripheral(int mio)
{
	const struct xslcr_mio *mio_ptr;
	unsigned long flags;
	int pin_set, pin, i;

	/* enable the peripheral only if it hasn't been already enabled */
	if (xslcr_test_bit(periph_status, mio))
		return -EBUSY;

	/* get the pin set */
	pin_set = active_pinset[mio];

	mio_ptr = &mio_periphs[mio];
	if (pin_set >= mio_ptr->max_sets) {
		pr_err("%s: Invalid pinset\n", mio_periph_name[mio]);
		return -EINVAL;
	}

	/* check whether all the pins in this pin set are unassigned */
	pin = mio_ptr->set_pins[pin_set]; /* 1st pin */
	for (i = 0; i < mio_ptr->numpins; i++) {
		if (xslcr_mio_isavailable(pin + i)) {
			pr_err("%s: One or more pins in pinset %d are busy\n",
				mio_periph_name[mio], pin_set);
			return -EBUSY;
		}
	}

	spin_lock_irqsave(&slcr->io_lock, flags);
	/* assign all pins in the set to this peripheral */
	for (i = 0; i < mio_ptr->numpins; i++)
		/* update the MIO register */
		xslcr_writereg((slcr->regs + ((pin + i) * 4) +
				XSLCR_MIO_PIN_00_OFFSET), mio_ptr->enable_val);

	/* all the pins in the pinset are configured for this peripheral.
	 * enable clocks */
	xslcr_enable_mio_clock(mio);

	/* mark that the peripheral has been enabled */
	xslcr_set_bit(periph_status, mio);
	spin_unlock_irqrestore(&slcr->io_lock, flags);

	pr_debug("Enabled peripheral %s on pinset %d\n",
		 mio_periph_name[mio], pin_set);
	return 0;
}

/**
 * xslcr_disable_mio_peripheral - Disable a MIO peripheral.
 *
 * @mio:	id used to lookup the data needed to enable the peripheral.
 *
 * This function checks if a MIO peripheral is previously enabled on the pinset
 * specified by the user, disables the peripheral and releases the MIO pins.
 *
 * return:	0 if the peripheral is disabled and MIO pins are released.
 *		negative error if the peripheral is already disabled, if an
 *		invalid peripheral is specified, or if the pins are assigned to
 *		a different peripheral.
 **/
static int xslcr_disable_mio_peripheral(int mio)
{
	const struct xslcr_mio *mio_ptr;
	unsigned long flags;
	int pin_set, pin, i;
	u32 reg;

	/* disable the peripheral only if it has been already enabled */
	if (!xslcr_test_bit(periph_status, mio))
		return -EBUSY;

	/* get the pin set */
	pin_set = active_pinset[mio];

	mio_ptr = &mio_periphs[mio];
	if (pin_set >= mio_ptr->max_sets) {
		pr_err("%s: Invalid pinset %d\n",
			mio_periph_name[mio], pin_set);
		return -EINVAL;
	}

	pin = mio_ptr->set_pins[pin_set]; /* 1st pin */
	for (i = 0; i < mio_ptr->numpins; i++) {

		/* check if each pin in the pin_set is assigned to this periph,
		 * to make sure the pins are not being released accidentally*/
		reg = xslcr_readreg(slcr->regs + XSLCR_MIO_PIN_00_OFFSET +
				    (pin * 4));
		reg &= XSLCR_MIO_LMASK;
		if (reg != mio_ptr->enable_val) {
			pr_err("%s: One or more pins in pinset %d are busy\n",
				mio_periph_name[mio], pin_set);
			return -EBUSY;
		}
	}

	spin_lock_irqsave(&slcr->io_lock, flags);
	/* release all pins in the set */
	for (i = 0; i < mio_ptr->numpins; i++) {

		/* update MIO register, set tri-state */
		xslcr_writereg((slcr->regs + ((pin + i) * 4) +
				XSLCR_MIO_PIN_00_OFFSET),
				xslcr_readreg((slcr->regs + ((pin + i) * 4) +
					XSLCR_MIO_PIN_00_OFFSET)) |
				XSLCR_MIO_PIN_XX_TRI_ENABLE);
	}

	/* all the pins in the set are released. disable clocks */
	xslcr_disable_mio_clock(mio);

	/* mark that the peripheral has been disabled */
	xslcr_clear_bit(periph_status, mio);
	spin_unlock_irqrestore(&slcr->io_lock, flags);

	pr_debug("Disabled peripheral %s on pinset %d\n",
		 mio_periph_name[mio], pin_set);
	return 0;
}

/**
 * xslcr_config_mio_peripheral - Enable/disable a MIO peripheral.
 *
 * @dev:	pointer to the MIO device.
 * @attr:	pointer to the 'enable_pinset' device attribute descriptor.
 * @buf:	pointer to the buffer with user data.
 * @size:	size of the buf.
 *
 * This function parses the user data in buf and enables/disables the MIO
 * peripheral specified by dev.
 *
 * return:	0 if the peripheral is enabled/disabled successfully.
 *		negative error if the peripheral configuration failed.
 **/
static ssize_t xslcr_config_mio_peripheral(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	unsigned long en;
	int mio, ret;

	/* get the peripheral */
	for (mio = 0; mio < ARRAY_SIZE(mio_periph_name); mio++) {
		if (sysfs_streq(dev_name(dev), mio_periph_name[mio]) == 1)
			break;
	}

	if (mio == ARRAY_SIZE(mio_periph_name)) {
		dev_err(dev, "Invalid peripheral specified\n");
		return -EINVAL;
	}

	ret = strict_strtoul(buf, 10, &en);
	if ((ret) || (en > 1)) {
		dev_err(dev, "Invalid user argument\n");
		return -EINVAL;
	}

	if (en == 1)
		ret = xslcr_enable_mio_peripheral(mio);
	else if (en == 0)
		ret = xslcr_disable_mio_peripheral(mio);

	return size;
}

static DEVICE_ATTR(enable_pinset, 0644, NULL, xslcr_config_mio_peripheral);

/**
 * xslcr_store_pinset - Store a pinset for a MIO peripheral.
 *
 * @dev:	pointer to the MIO device.
 * @attr:	pointer to the 'pinset' device attribute descriptor.
 * @buf:	pointer to the buffer with user data.
 * @size:	size of the buf.
 *
 * This function parses the user data in buf and stores the pinset for the MIO
 * peripheral specified by dev. This pinset will be later used to enable or
 * disable the MIO peripheral.
 *
 * return:	0 if the peripheral is enabled/disabled successfully.
 *		negative error if the peripheral configuration failed.
 **/
static ssize_t xslcr_store_pinset(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	unsigned long pin_set;
	int mio, ret;

	/* get the peripheral */
	for (mio = 0; mio < ARRAY_SIZE(mio_periph_name); mio++) {
		if (sysfs_streq(dev_name(dev), mio_periph_name[mio]) == 1)
			break;
	}

	if (mio == ARRAY_SIZE(mio_periph_name)) {
		dev_err(dev, "Invalid peripheral specified\n");
		return -EINVAL;
	}

	/* get the pin set */
	ret = strict_strtoul(buf, 10, &pin_set);
	if ((ret) || (pin_set >= mio_periphs[mio].max_sets)) {
		dev_err(dev, "Invalid pinset\n");
		return -EINVAL;
	}

	/* store the pin set */
	active_pinset[mio] = pin_set;
	dev_dbg(dev, "Pinset=%d\n", (unsigned int)pin_set);

	return size;
}

static DEVICE_ATTR(pinset, 0644, NULL, xslcr_store_pinset);

/**
 * xslcr_config_mio_clock - Enable/disable the clocks for a MIO peripheral.
 *
 * @dev:	pointer to this device.
 * @attr:	pointer to the device attribute descriptor.
 * @buf:	pointer to the buffer with user data.
 * @size:	size of the buf.
 *
 * This function parses the user buffer and enables/disables the clocks of a MIO
 * peripheral specified by the user.
 *
 * return: negative error if invalid arguments are specified or size of the buf
 * if the clocks are enabled/disabled successfully.
 **/
static ssize_t xslcr_config_mio_clock(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	unsigned long flags, en;
	int mio, ret;

	/* check if a valid peripheral is specified */
	for (mio = 0; mio < ARRAY_SIZE(mio_periph_name); mio++) {
		if (sysfs_streq(dev_name(dev), mio_periph_name[mio]) == 1)
			break;
	}

	if (mio == ARRAY_SIZE(mio_periph_name)) {
		dev_err(dev, "Invalid peripheral specified\n");
		return -EINVAL;
	}

	ret = strict_strtoul(buf, 10, &en);
	if ((ret) || (en > 1)) {
		dev_err(dev, "Invalid user argument\n");
		return -EINVAL;
	}

	/* enable/disable the clocks */
	spin_lock_irqsave(&slcr->io_lock, flags);
	if (en == 1)
		xslcr_enable_mio_clock(mio);
	else if (en == 0)
		xslcr_disable_mio_clock(mio);

	spin_unlock_irqrestore(&slcr->io_lock, flags);
	return size;
}

static DEVICE_ATTR(clock, 0644, NULL, xslcr_config_mio_clock);

/**
 * xslcr_get_periph_status - Get the current status of a MIO peripheral.
 *
 * @dev:	pointer to this device.
 * @attr:	pointer to the device attribute descriptor.
 * @buf:	pointer to the buffer in which pin status is returned as a str.
 *
 * This function returns the current status of a MIO peripheral specified by the
 * user.
 *
 * return:	negative error if an invalid peripheral is specified or size of
 *		the buf, with the status of the peripheral.
 **/
static ssize_t xslcr_get_periph_status(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	ssize_t size;
	int mio;

	/* check if a valid peripheral is specified */
	for (mio = 0; mio < ARRAY_SIZE(mio_periph_name); mio++) {
		if (sysfs_streq(dev_name(dev), mio_periph_name[mio]) == 1)
			break;
	}

	if (mio == ARRAY_SIZE(mio_periph_name)) {
		dev_err(dev, "Invalid peripheral specified\n");
		return -EINVAL;
	}

	size = sprintf(buf, "%d\n", xslcr_test_bit(periph_status, mio));

	return size;
}

static DEVICE_ATTR(status, 0644, xslcr_get_periph_status, NULL);

/**
 * xslcr_reset_periph - Reset a peripheral within PS.
 *
 * @dev:	pointer to this device.
 * @attr:	pointer to the device attribute descriptor.
 * @buf:	pointer to the buffer with user data.
 * @size:	size of the buf.
 *
 * This function performs a software reset on the peripheral specified by the
 * user.
 *
 * return: negative error if an invalid peripheral is specified or size of the
 * buf if the peripheral is reset successfully.
 **/
static ssize_t xslcr_reset_periph(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	unsigned long flags, rst;
	int i, ret;
	u32 reg;

	/* check for a valid peripheral */
	for (i = 0; i < ARRAY_SIZE(reset_periph_name); i++) {
		if (sysfs_streq(dev_name(dev), reset_periph_name[i]) == 1)
			break;
	}

	if (i == ARRAY_SIZE(reset_periph_name)) {
		dev_err(dev, "Invalid peripheral specified\n");
		return -EINVAL;
	}

	ret = strict_strtoul(buf, 10, &rst);
	if (ret) {
		dev_err(dev, "Invalid user argument\n");
		return -EINVAL;
	}

	/* reset the peripheral */
	spin_lock_irqsave(&slcr->io_lock, flags);

	/* read the register and modify only the specified bit */
	reg = xslcr_readreg(slcr->regs + reset_info[i].reg_offset);
	if (!rst)
		reg &= ~(reset_info[i].reset_mask);
	else
		reg |= reset_info[i].reset_mask;

	xslcr_writereg(slcr->regs + reset_info[i].reg_offset, reg);

	spin_unlock_irqrestore(&slcr->io_lock, flags);
	return size;
}

static DEVICE_ATTR(reset, 0644, NULL, xslcr_reset_periph);

/**
 * show_mio_pin_status - Get the status of all the MIO pins.
 *
 * @dev:	pointer to this device.
 * @attr:	pointer to the device attribute descriptor.
 * @buf:	pointer to the buffer in which pin status is returned as a str.
 *
 * This function returns overall status of the MIO pins as a 64-bit mask. Bit
 * positions with 1 indicate that the corresponding MIO pin has been assigned to
 * a peripheral and bit positions with 0 indicate that the pin is free.
 *
 * return:	length of the buffer containing the mio pin status.
 **/
static ssize_t show_mio_pin_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u64 pin_status = 0;
	ssize_t status;
	u32 reg;
	int i;

	for (i = 0; i < 54; i++) {

		/* read the MIO control register to determine if its free */
		reg = xslcr_readreg((slcr->regs + (i * 4) +
				    XSLCR_MIO_PIN_00_OFFSET));
		if (!(reg & XSLCR_MIO_PIN_XX_TRI_ENABLE))
			xslcr_set_bit((u32 *)&pin_status, i);
	}
	status = sprintf(buf, "0x%016Lx\n", pin_status);

	return status;
}

static DEVICE_ATTR(mio_pin_status, 0644, show_mio_pin_status, NULL);

/* MIO attributes */
static const struct attribute *xslcr_mio_attrs[] = {
	&dev_attr_enable_pinset.attr,
	&dev_attr_pinset.attr,
	&dev_attr_clock.attr,
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group xslcr_mio_attr_group = {
	.attrs = (struct attribute **) xslcr_mio_attrs,
};

/* MIO class */
static struct class xslcr_mio_class = {
	.name =		"xslcr_mio",
	.owner =	THIS_MODULE,
};

/* Reset class */
static struct class xslcr_reset_class = {
	.name =		"xslcr_reset",
	.owner =	THIS_MODULE,
};

/**
 * match_dev - Match function for finding a device
 *
 * @dev:	Device to find.
 * @data:	Device private data used for finding the device.
 */
static int match_dev(struct device *dev, void *data)
{
	return dev_get_drvdata(dev) == data;
}

/**
 * xslcr_create_devices - Create devices and their sysfs files.
 *
 * @dev:	pointer to the platform device structure.
 * @xslcr_class:pointer to the class that the devices should be registered to.
 * @buf:	pointer to the array of device names.
 * @nr:		number of devices that should be created.
 *
 * This function creates devices for MIO peripherals or reset peripherals and
 * registers them to their respective classes. It also creates sysfs files for
 * each of these devices.
 *
 * return:	0 if all the devices and sysfs files are created successfully.
 *		negative error if the devices or their sysfs files can't be
 *		created.
 **/
static int xslcr_create_devices(struct platform_device *pdev,
				struct class *xslcr_class,
				const char **periph, int nr)
{
	int i, ret;

	for (i = 0; i < nr; i++) {
		struct device	*dev;

		dev = device_create(xslcr_class, &pdev->dev, MKDEV(0, 0),
				    (void *)(periph[i]), periph[i], i);
		if (!IS_ERR(dev)) {
			dev_set_drvdata(dev, (void *)(periph[i]));
			if (xslcr_class == &xslcr_mio_class) {
				ret = sysfs_create_group(&dev->kobj,
							 &xslcr_mio_attr_group);
			} else {
				ret = device_create_file(dev, &dev_attr_reset);
			}

			if (ret != 0) {
				device_unregister(dev);
				dev_err(dev, "Failed to create sysfs attrs\n");
				return ret;
			}
		} else
			return PTR_ERR(dev);
	}

	return 0;
}

/**
 * xslcr_remove_devices - Remove devices and their sysfs files.
 *
 * @dev:	pointer to the platform device structure.
 * @xslcr_class:pointer to the class that the devices should be registered to.
 * @buf:	pointer to the array of device names.
 * @nr:		number of devices that should be created.
 *
 * This function removes devices and sysfs files created by xslcr_create_devices
 * It also unregisters the class to which these devices were registered to.
 *
 * return:	0 if all the devices and sysfs files are removed successfully.
 *		negative error if the devices or their sysfs files can't be
 *		removed.
 **/
static void xslcr_remove_devices(struct class *xslcr_class,
				 const char **periph, int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		struct device	*dev = NULL;

		dev = class_find_device(xslcr_class, NULL, (void *)(periph[i]),
					match_dev);
		if (dev) {
			if (xslcr_class == &xslcr_mio_class) {
				sysfs_remove_group(&dev->kobj,
						   &xslcr_mio_attr_group);
			} else {
				device_remove_file(dev, &dev_attr_reset);
			}
			put_device(dev);
			device_unregister(dev);
		}
	}

	class_unregister(xslcr_class);
}

/**
 * xslcr_get_mio_status - Initialize periph_status
 *
 * Read all the MIO control registers and determine which MIO peripherals are
 * enabled and initialize the global array .
 **/

static void xslcr_get_mio_status(void)
{
	const struct xslcr_mio *mio_ptr;
	u32 mio_reg;
	int i, j, k;

	/* num pins */
	for (i = 0; i < XSLCR_MIO_MAX_PIN;) {
		mio_reg = xslcr_readreg(slcr->regs + (i * 4) +
					XSLCR_MIO_PIN_00_OFFSET);
		if (mio_reg & XSLCR_MIO_PIN_XX_TRI_ENABLE) {
			i++;
			continue;
		}
		mio_reg &= XSLCR_MIO_LMASK;
		/* num periphs */
		for (j = 0; j < ARRAY_SIZE(mio_periphs); j++) {
			if (mio_reg == mio_periphs[j].enable_val) {
				mio_ptr = &mio_periphs[j];
				for (k = 0; k < mio_ptr->max_sets; k++) {
					if (i == mio_ptr->set_pins[k]) {
						/* mark the periph as enabled */
						xslcr_set_bit(periph_status, j);
						active_pinset[j] = k;
						i += mio_ptr->numpins;
						goto next_periph;
					}
				}
			}
		}
		/*Noone claims this pin*/
		printk(KERN_INFO "MIO pin %2d not assigned(%08x)\n",
			i,
			xslcr_readreg(slcr->regs + (i * 4) +
				XSLCR_MIO_PIN_00_OFFSET)
			);
		i++;
next_periph:
		continue;
	}
}

/************************Platform Operations*****************************/
/**
 * xslcr_probe - Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * This fucntion allocates resources for the SLCR device and creates sysfs
 * attributes for the functionality available in the SLCR block. User can
 * write to these sysfs files to enable/diable mio peripherals/cocks, reset
 * peripherals, etc.
 *
 * Return: 0 on success, negative error otherwise.
 **/
static int __devinit xslcr_probe(struct platform_device *pdev)
{
	struct resource res;
	int ret;

	res.start = 0xF8000000;
	res.end = 0xF8000FFF;

	if (slcr) {
		dev_err(&pdev->dev, "Device Busy, only 1 slcr instance "
			"supported.\n");
		return -EBUSY;
	}

	if (!request_mem_region(res.start,
					res.end - res.start + 1,
					DRIVER_NAME)) {
		dev_err(&pdev->dev, "Couldn't lock memory region at %Lx\n",
			(unsigned long long)res.start);
		return -EBUSY;
	}

	slcr = kzalloc(sizeof(struct xslcr), GFP_KERNEL);
	if (!slcr) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Unable to allocate memory for driver "
			"data\n");
		goto err_release;
	}

	slcr->regs = ioremap(res.start, (res.end - res.start + 1));
	if (!slcr->regs) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Unable to map I/O memory\n");
		goto err_free;
	}

	/* init periph_status based on the data from MIO control registers */
	xslcr_get_mio_status();

	spin_lock_init(&slcr->io_lock);

	ret = class_register(&xslcr_mio_class);
	if (ret < 0)
		goto err_iounmap;

	ret = xslcr_create_devices(pdev, &xslcr_mio_class, mio_periph_name,
				   ARRAY_SIZE(mio_periph_name));
	if (ret)
		goto err_mio_class;

	ret = class_register(&xslcr_reset_class);
	if (ret < 0)
		goto err_mio_class;

	ret = xslcr_create_devices(pdev, &xslcr_reset_class, reset_periph_name,
				   ARRAY_SIZE(reset_periph_name));
	if (ret)
		goto err_rst_class;

	ret = device_create_file(&pdev->dev, &dev_attr_mio_pin_status);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs attr\n");
		goto err_rst_class;
	}

	/* unlock the SLCR so that registers can be changed */
	xslcr_writereg(slcr->regs + XSLCR_UNLOCK, 0xDF0D);

	dev_info(&pdev->dev, "at 0x%08X mapped to 0x%08X\n", res.start,
		 (u32 __force)slcr->regs);
	platform_set_drvdata(pdev, slcr);

	return 0;

err_rst_class:
	xslcr_remove_devices(&xslcr_reset_class, reset_periph_name,
			     ARRAY_SIZE(reset_periph_name));
err_mio_class:
	xslcr_remove_devices(&xslcr_mio_class, mio_periph_name,
			     ARRAY_SIZE(mio_periph_name));
err_iounmap:
	iounmap(slcr->regs);
err_free:
	kfree(slcr);
err_release:
	release_mem_region(res.start, (res.end - res.start + 1));

	return ret;
}

/**
 * xslcr_remove -  Remove call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * Unregister the device after releasing the resources.
 * Returns 0 on success, otherwise negative error.
 **/
static int __devexit xslcr_remove(struct platform_device *pdev)
{
	struct xslcr *id = platform_get_drvdata(pdev);
	struct resource *res;

	device_remove_file(&pdev->dev, &dev_attr_mio_pin_status);

	xslcr_remove_devices(&xslcr_reset_class, reset_periph_name,
			     ARRAY_SIZE(reset_periph_name));
	xslcr_remove_devices(&xslcr_mio_class, mio_periph_name,
			     ARRAY_SIZE(mio_periph_name));

	iounmap(id->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to locate mmio resource\n");
		return -ENODEV;
	}
	release_mem_region(res->start, resource_size(res));

	kfree(id);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

/* Driver Structure */
static struct platform_driver xslcr_driver = {
	.probe		= xslcr_probe,
	.remove		= __devexit_p(xslcr_remove),
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

struct platform_device xslcr_device = {
	.name = "xslcr",
	.dev.platform_data = NULL,
};

/**
 * xslcr_init -  Register the SLCR.
 *
 * Returns 0 on success, otherwise negative error.
 */
static int __init xslcr_init(void)
{
	platform_device_register(&xslcr_device);
	return platform_driver_register(&xslcr_driver);
}
arch_initcall(xslcr_init);

/**
 * xslcr_exit -  Unregister the SLCR.
 */
static void __exit xslcr_exit(void)
{
	platform_driver_unregister(&xslcr_driver);
}



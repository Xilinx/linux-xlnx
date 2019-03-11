/*
 * Xilinx Zynq UltraScale+ MPSoC Quad-SPI (QSPI) controller driver
 * (master mode only)
 *
 * Copyright (C) 2009 - 2015 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/firmware/xlnx-zynqmp.h>

/* Generic QSPI register offsets */
#define GQSPI_CONFIG_OFST		0x00000100
#define GQSPI_ISR_OFST			0x00000104
#define GQSPI_IDR_OFST			0x0000010C
#define GQSPI_IER_OFST			0x00000108
#define GQSPI_IMASK_OFST		0x00000110
#define GQSPI_EN_OFST			0x00000114
#define GQSPI_TXD_OFST			0x0000011C
#define GQSPI_RXD_OFST			0x00000120
#define GQSPI_TX_THRESHOLD_OFST		0x00000128
#define GQSPI_RX_THRESHOLD_OFST		0x0000012C
#define IOU_TAPDLY_BYPASS_OFST		0x0000003C
#define GQSPI_LPBK_DLY_ADJ_OFST		0x00000138
#define GQSPI_GEN_FIFO_OFST		0x00000140
#define GQSPI_SEL_OFST			0x00000144
#define GQSPI_GF_THRESHOLD_OFST		0x00000150
#define GQSPI_FIFO_CTRL_OFST		0x0000014C
#define GQSPI_QSPIDMA_DST_CTRL_OFST	0x0000080C
#define GQSPI_QSPIDMA_DST_SIZE_OFST	0x00000804
#define GQSPI_QSPIDMA_DST_STS_OFST	0x00000808
#define GQSPI_QSPIDMA_DST_I_STS_OFST	0x00000814
#define GQSPI_QSPIDMA_DST_I_EN_OFST	0x00000818
#define GQSPI_QSPIDMA_DST_I_DIS_OFST	0x0000081C
#define GQSPI_QSPIDMA_DST_I_MASK_OFST	0x00000820
#define GQSPI_QSPIDMA_DST_ADDR_OFST	0x00000800
#define GQSPI_QSPIDMA_DST_ADDR_MSB_OFST 0x00000828
#define GQSPI_DATA_DLY_ADJ_OFST		0x000001F8

/* GQSPI register bit masks */
#define GQSPI_SEL_MASK				0x00000001
#define GQSPI_EN_MASK				0x00000001
#define GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK	0x00000020
#define GQSPI_ISR_WR_TO_CLR_MASK		0x00000002
#define GQSPI_IDR_ALL_MASK			0x00000FBE
#define GQSPI_CFG_MODE_EN_MASK			0xC0000000
#define GQSPI_CFG_GEN_FIFO_START_MODE_MASK	0x20000000
#define GQSPI_CFG_ENDIAN_MASK			0x04000000
#define GQSPI_CFG_EN_POLL_TO_MASK		0x00100000
#define GQSPI_CFG_WP_HOLD_MASK			0x00080000
#define GQSPI_CFG_BAUD_RATE_DIV_MASK		0x00000038
#define GQSPI_CFG_CLK_PHA_MASK			0x00000004
#define GQSPI_CFG_CLK_POL_MASK			0x00000002
#define GQSPI_CFG_START_GEN_FIFO_MASK		0x10000000
#define GQSPI_GENFIFO_IMM_DATA_MASK		0x000000FF
#define GQSPI_GENFIFO_DATA_XFER			0x00000100
#define GQSPI_GENFIFO_EXP			0x00000200
#define GQSPI_GENFIFO_MODE_SPI			0x00000400
#define GQSPI_GENFIFO_MODE_DUALSPI		0x00000800
#define GQSPI_GENFIFO_MODE_QUADSPI		0x00000C00
#define GQSPI_GENFIFO_MODE_MASK			0x00000C00
#define GQSPI_GENFIFO_CS_LOWER			0x00001000
#define GQSPI_GENFIFO_CS_UPPER			0x00002000
#define GQSPI_GENFIFO_BUS_LOWER			0x00004000
#define GQSPI_GENFIFO_BUS_UPPER			0x00008000
#define GQSPI_GENFIFO_BUS_BOTH			0x0000C000
#define GQSPI_GENFIFO_BUS_MASK			0x0000C000
#define GQSPI_GENFIFO_TX			0x00010000
#define GQSPI_GENFIFO_RX			0x00020000
#define GQSPI_GENFIFO_STRIPE			0x00040000
#define GQSPI_GENFIFO_POLL			0x00080000
#define GQSPI_GENFIFO_EXP_START			0x00000100
#define GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK	0x00000004
#define GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK	0x00000002
#define GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK	0x00000001
#define GQSPI_ISR_RXEMPTY_MASK			0x00000800
#define GQSPI_ISR_GENFIFOFULL_MASK		0x00000400
#define GQSPI_ISR_GENFIFONOT_FULL_MASK		0x00000200
#define GQSPI_ISR_TXEMPTY_MASK			0x00000100
#define GQSPI_ISR_GENFIFOEMPTY_MASK		0x00000080
#define GQSPI_ISR_RXFULL_MASK			0x00000020
#define GQSPI_ISR_RXNEMPTY_MASK			0x00000010
#define GQSPI_ISR_TXFULL_MASK			0x00000008
#define GQSPI_ISR_TXNOT_FULL_MASK		0x00000004
#define GQSPI_ISR_POLL_TIME_EXPIRE_MASK		0x00000002
#define GQSPI_IER_TXNOT_FULL_MASK		0x00000004
#define GQSPI_IER_RXEMPTY_MASK			0x00000800
#define GQSPI_IER_POLL_TIME_EXPIRE_MASK		0x00000002
#define GQSPI_IER_RXNEMPTY_MASK			0x00000010
#define GQSPI_IER_GENFIFOEMPTY_MASK		0x00000080
#define GQSPI_IER_TXEMPTY_MASK			0x00000100
#define GQSPI_QSPIDMA_DST_INTR_ALL_MASK		0x000000FE
#define GQSPI_QSPIDMA_DST_STS_WTC		0x0000E000
#define GQSPI_CFG_MODE_EN_DMA_MASK		0x80000000
#define GQSPI_ISR_IDR_MASK			0x00000994
#define GQSPI_QSPIDMA_DST_I_EN_DONE_MASK	0x00000002
#define GQSPI_QSPIDMA_DST_I_STS_DONE_MASK	0x00000002
#define GQSPI_IRQ_MASK				0x00000980

#define GQSPI_CFG_BAUD_RATE_DIV_SHIFT		3
#define GQSPI_GENFIFO_CS_SETUP			0x4
#define GQSPI_GENFIFO_CS_HOLD			0x3
#define GQSPI_TXD_DEPTH				64
#define GQSPI_RX_FIFO_THRESHOLD			32
#define GQSPI_RX_FIFO_FILL	(GQSPI_RX_FIFO_THRESHOLD * 4)
#define GQSPI_TX_FIFO_THRESHOLD_RESET_VAL	32
#define GQSPI_TX_FIFO_FILL	(GQSPI_TXD_DEPTH -\
				GQSPI_TX_FIFO_THRESHOLD_RESET_VAL)
#define GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL	0X10
#define GQSPI_QSPIDMA_DST_CTRL_RESET_VAL	0x803FFA00
#define GQSPI_SELECT_FLASH_CS_LOWER		0x1
#define GQSPI_SELECT_FLASH_CS_UPPER		0x2
#define GQSPI_SELECT_FLASH_CS_BOTH		0x3
#define GQSPI_SELECT_FLASH_BUS_LOWER		0x1
#define GQSPI_SELECT_FLASH_BUS_UPPER		0x2
#define GQSPI_SELECT_FLASH_BUS_BOTH		0x3
#define GQSPI_BAUD_DIV_MAX	7	/* Baud rate divisor maximum */
#define GQSPI_BAUD_DIV_SHIFT	2	/* Baud rate divisor shift */
#define GQSPI_SELECT_MODE_SPI		0x1
#define GQSPI_SELECT_MODE_DUALSPI	0x2
#define GQSPI_SELECT_MODE_QUADSPI	0x4
#define GQSPI_DMA_UNALIGN		0x3
#define GQSPI_DEFAULT_NUM_CS	1	/* Default number of chip selects */
#define GQSPI_RX_BUS_WIDTH_QUAD		0x4
#define GQSPI_RX_BUS_WIDTH_DUAL		0x2
#define GQSPI_RX_BUS_WIDTH_SINGLE	0x1
#define GQSPI_TX_BUS_WIDTH_QUAD		0x4
#define GQSPI_TX_BUS_WIDTH_DUAL		0x2
#define GQSPI_TX_BUS_WIDTH_SINGLE	0x1
#define GQSPI_LPBK_DLY_ADJ_LPBK_SHIFT	5
#define GQSPI_LPBK_DLY_ADJ_DLY_1	0x2
#define GQSPI_LPBK_DLY_ADJ_DLY_1_SHIFT	3
#define GQSPI_LPBK_DLY_ADJ_DLY_0	0x3
#define GQSPI_USE_DATA_DLY		0x1
#define GQSPI_USE_DATA_DLY_SHIFT	31
#define GQSPI_DATA_DLY_ADJ_VALUE	0x2
#define GQSPI_DATA_DLY_ADJ_SHIFT	28
#define TAP_DLY_BYPASS_LQSPI_RX_VALUE	0x1
#define TAP_DLY_BYPASS_LQSPI_RX_SHIFT	2

/* set to differentiate versal from zynqmp, 1=versal, 0=zynqmp */
#define QSPI_QUIRK_HAS_TAPDELAY	BIT(0)

#define GQSPI_FREQ_40MHZ	40000000
#define GQSPI_FREQ_100MHZ	100000000
#define GQSPI_FREQ_150MHZ	150000000
#define IOU_TAPDLY_BYPASS_MASK	0x7

#define SPI_AUTOSUSPEND_TIMEOUT		3000
enum mode_type {GQSPI_MODE_IO, GQSPI_MODE_DMA};
static const struct zynqmp_eemi_ops *eemi_ops;

/**
 * struct zynq_platform_data - zynqmp qspi platform data structure
 * @quirks:	Flags is used to identify the platform
 */
struct qspi_platform_data {
	u32 quirks;
};

/**
 * struct zynqmp_qspi - Defines qspi driver instance
 * @regs:		Virtual address of the QSPI controller registers
 * @refclk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @irq:		IRQ number
 * @dev:		Pointer to struct device
 * @txbuf:		Pointer to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @genfifocs:		Used for chip select
 * @genfifobus:		Used to select the upper or lower bus
 * @dma_rx_bytes:	Remaining bytes to receive by DMA mode
 * @dma_addr:		DMA address after mapping the kernel buffer
 * @tx_bus_width:	Used to represent number of data wires for tx
 * @rx_bus_width:	Used to represent number of data wires
 * @genfifoentry:	Used for storing the genfifoentry instruction.
 * @isinstr:		To determine whether the transfer is instruction
 * @mode:		Defines the mode in which QSPI is operating
 * @speed_hz:		Current SPI bus clock speed in hz
 * @io_mode:		Defines the operating mode, either IO or dma
 * @has_tapdelay:	Used for tapdelay register available in qspi
 */
struct zynqmp_qspi {
	void __iomem *regs;
	struct clk *refclk;
	struct clk *pclk;
	int irq;
	struct device *dev;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u32 genfifocs;
	u32 genfifobus;
	u32 dma_rx_bytes;
	dma_addr_t dma_addr;
	u32 rx_bus_width;
	u32 tx_bus_width;
	u32 genfifoentry;
	bool isinstr;
	enum mode_type mode;
	u32 speed_hz;
	bool io_mode;
	bool has_tapdelay;
};

/**
 * zynqmp_gqspi_read -	For GQSPI controller read operation
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @offset:	Offset from where to read
 *
 * Return: Value read from the qspi register
 */
static u32 zynqmp_gqspi_read(struct zynqmp_qspi *xqspi, u32 offset)
{
	return readl_relaxed(xqspi->regs + offset);
}

/**
 * zynqmp_gqspi_write -	For GQSPI controller write operation
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @offset:	Offset where to write
 * @val:	Value to be written
 */
static inline void zynqmp_gqspi_write(struct zynqmp_qspi *xqspi, u32 offset,
				      u32 val)
{
	writel_relaxed(val, (xqspi->regs + offset));
}

/**
 * zynqmp_gqspi_selectslave -	For selection of slave device
 * @instanceptr:	Pointer to the zynqmp_qspi structure
 * @slavecs:	For chip select
 * @slavebus:	To check which bus is selected- upper or lower
 */
static void zynqmp_gqspi_selectslave(struct zynqmp_qspi *instanceptr,
				     u8 slavecs, u8 slavebus)
{
	/*
	 * Bus and CS lines selected here will be updated in the instance and
	 * used for subsequent GENFIFO entries during transfer.
	 */

	/* Choose slave select line */
	switch (slavecs) {
	case GQSPI_SELECT_FLASH_CS_BOTH:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER |
			GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_UPPER:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_LOWER:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER;
		break;
	default:
		dev_warn(instanceptr->dev, "Invalid slave select\n");
	}

	/* Choose the bus */
	switch (slavebus) {
	case GQSPI_SELECT_FLASH_BUS_BOTH:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER |
			GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_UPPER:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_LOWER:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER;
		break;
	default:
		dev_warn(instanceptr->dev, "Invalid slave bus\n");
	}
}

/**
 * zynqmp_qspi_set_tapdelay -	To configure qspi tap delays
 * @xqspi:		Pointer to the zynqmp_qspi structure
 * @baudrateval:	Buadrate to configure
 */
static void zynqmp_qspi_set_tapdelay(struct zynqmp_qspi *xqspi, u32 baudrateval)
{
	u32 tapdlybypass = 0, lpbkdlyadj = 0, datadlyadj = 0, clk_rate;
	u32 reqhz = 0;

	if (!eemi_ops->ioctl)
		return;

	clk_rate = clk_get_rate(xqspi->refclk);
	reqhz = (clk_rate / (GQSPI_BAUD_DIV_SHIFT << baudrateval));

	if (!xqspi->has_tapdelay) {
		if (reqhz <= GQSPI_FREQ_40MHZ) {
			eemi_ops->ioctl(NODE_QSPI, IOCTL_SET_TAPDELAY_BYPASS,
					   PM_TAPDELAY_QSPI,
					   PM_TAPDELAY_BYPASS_ENABLE,
					   NULL);
		} else if (reqhz <= GQSPI_FREQ_100MHZ) {
			eemi_ops->ioctl(NODE_QSPI, IOCTL_SET_TAPDELAY_BYPASS,
					   PM_TAPDELAY_QSPI,
					   PM_TAPDELAY_BYPASS_ENABLE,
					   NULL);
			lpbkdlyadj |= (GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
			datadlyadj |= ((GQSPI_USE_DATA_DLY <<
					GQSPI_USE_DATA_DLY_SHIFT)
					| (GQSPI_DATA_DLY_ADJ_VALUE <<
						GQSPI_DATA_DLY_ADJ_SHIFT));
		} else if (reqhz <= GQSPI_FREQ_150MHZ) {
			lpbkdlyadj |= GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK;
		}
	} else {
		if (reqhz <= GQSPI_FREQ_40MHZ) {
			tapdlybypass |= (TAP_DLY_BYPASS_LQSPI_RX_VALUE <<
					TAP_DLY_BYPASS_LQSPI_RX_SHIFT);
		} else if (reqhz <= GQSPI_FREQ_100MHZ) {
			tapdlybypass |= (TAP_DLY_BYPASS_LQSPI_RX_VALUE <<
					TAP_DLY_BYPASS_LQSPI_RX_SHIFT);
			lpbkdlyadj |= (GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
			datadlyadj |= ((GQSPI_USE_DATA_DLY <<
					GQSPI_USE_DATA_DLY_SHIFT)
					| (GQSPI_DATA_DLY_ADJ_VALUE <<
						GQSPI_DATA_DLY_ADJ_SHIFT));
		} else if (reqhz <= GQSPI_FREQ_150MHZ) {
			lpbkdlyadj |= GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK;
		}
		zynqmp_gqspi_write(xqspi,
				IOU_TAPDLY_BYPASS_OFST, tapdlybypass);
	}

	zynqmp_gqspi_write(xqspi, GQSPI_LPBK_DLY_ADJ_OFST, lpbkdlyadj);
	zynqmp_gqspi_write(xqspi, GQSPI_DATA_DLY_ADJ_OFST, datadlyadj);
}

static u32 zynqmp_disable_intr(struct zynqmp_qspi *xqspi)
{
	u32 value;

	zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_ISR_IDR_MASK);
	value = zynqmp_gqspi_read(xqspi, GQSPI_IMASK_OFST);
	zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_ISR_IDR_MASK);

	return value;
}

/**
 * zynqmp_qspi_init_hw -	Initialize the hardware
 * @xqspi:	Pointer to the zynqmp_qspi structure
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Master mode
 *	- TX threshold set to 1
 *	- RX threshold set to 1
 *	- Flash memory interface mode enabled
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual slave select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the little endian mode of TX FIFO and
 *	- Enable the QSPI controller
 */
static void zynqmp_qspi_init_hw(struct zynqmp_qspi *xqspi)
{
	u32 config_reg;

	/* Select the GQSPI mode */
	zynqmp_gqspi_write(xqspi, GQSPI_SEL_OFST, GQSPI_SEL_MASK);
	/* Clear and disable interrupts */
	zynqmp_disable_intr(xqspi);
	/* Clear the DMA STS */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_I_STS_OFST));
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_STS_OFST) |
					     GQSPI_QSPIDMA_DST_STS_WTC);
	zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_IDR_ALL_MASK);
	zynqmp_gqspi_write(xqspi,
			   GQSPI_QSPIDMA_DST_I_DIS_OFST,
			   GQSPI_QSPIDMA_DST_INTR_ALL_MASK);
	/* Disable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);
	config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	/* Manual start */
	config_reg |= GQSPI_CFG_GEN_FIFO_START_MODE_MASK;
	/* Little endian by default */
	config_reg &= ~GQSPI_CFG_ENDIAN_MASK;
	/* Disable poll time out */
	config_reg &= ~GQSPI_CFG_EN_POLL_TO_MASK;
	/* Set hold bit */
	config_reg |= GQSPI_CFG_WP_HOLD_MASK;
	/* Clear pre-scalar by default */
	config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
	/* CPHA 0 */
	config_reg &= ~GQSPI_CFG_CLK_PHA_MASK;
	/* CPOL 0 */
	config_reg &= ~GQSPI_CFG_CLK_POL_MASK;
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);

	/* Clear the TX and RX FIFO */
	zynqmp_gqspi_write(xqspi, GQSPI_FIFO_CTRL_OFST,
			   GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK);
	/* Set by default to allow for high frequencies */
	zynqmp_gqspi_write(xqspi, GQSPI_LPBK_DLY_ADJ_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_LPBK_DLY_ADJ_OFST) |
			   GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
	/* Reset thresholds */
	zynqmp_gqspi_write(xqspi, GQSPI_TX_THRESHOLD_OFST,
			   GQSPI_TX_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_write(xqspi, GQSPI_RX_THRESHOLD_OFST,
			   GQSPI_RX_FIFO_THRESHOLD);
	zynqmp_gqspi_write(xqspi, GQSPI_GF_THRESHOLD_OFST,
			   GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_selectslave(xqspi,
				 GQSPI_SELECT_FLASH_CS_LOWER,
				 GQSPI_SELECT_FLASH_BUS_LOWER);
	if (!xqspi->io_mode) {
		/* Initialize DMA */
		zynqmp_gqspi_write(xqspi,
			GQSPI_QSPIDMA_DST_CTRL_OFST,
			GQSPI_QSPIDMA_DST_CTRL_RESET_VAL);
	}
	/* Enable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);
}

/**
 * zynqmp_qspi_copy_read_data -	Copy data to RX buffer
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @data:	The variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 */
static void zynqmp_qspi_copy_read_data(struct zynqmp_qspi *xqspi,
				       ulong data, u8 size)
{
	memcpy(xqspi->rxbuf, &data, size);
	xqspi->rxbuf += size;
	xqspi->bytes_to_receive -= size;
}

/**
 * zynqmp_prepare_transfer_hardware -	Prepares hardware for transfer.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function enables SPI master controller.
 *
 * Return:	0 on success; error value otherwise
 */
static int zynqmp_prepare_transfer_hardware(struct spi_master *master)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);
	return 0;
}

/**
 * zynqmp_unprepare_transfer_hardware -	Relaxes hardware after transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function disables the SPI master controller.
 *
 * Return:	Always 0
 */
static int zynqmp_unprepare_transfer_hardware(struct spi_master *master)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);
	return 0;
}

/**
 * zynqmp_qspi_chipselect -	Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 */
static void zynqmp_qspi_chipselect(struct spi_device *qspi, bool is_high)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(qspi->master);
	ulong timeout;
	u32 genfifoentry = 0x0, statusreg;

	genfifoentry |= GQSPI_GENFIFO_MODE_SPI;

	if (qspi->master->flags & SPI_MASTER_BOTH_CS) {
		zynqmp_gqspi_selectslave(xqspi,
			GQSPI_SELECT_FLASH_CS_BOTH,
			GQSPI_SELECT_FLASH_BUS_BOTH);
	} else if (qspi->master->flags & SPI_MASTER_U_PAGE) {
		zynqmp_gqspi_selectslave(xqspi,
			GQSPI_SELECT_FLASH_CS_UPPER,
			GQSPI_SELECT_FLASH_BUS_LOWER);
	} else {
		zynqmp_gqspi_selectslave(xqspi,
			GQSPI_SELECT_FLASH_CS_LOWER,
			GQSPI_SELECT_FLASH_BUS_LOWER);
	}

	genfifoentry |= xqspi->genfifobus;

	if (!is_high) {
		genfifoentry |= xqspi->genfifocs;
		genfifoentry |= GQSPI_GENFIFO_CS_SETUP;
		xqspi->isinstr = true;
	} else {
		genfifoentry |= GQSPI_GENFIFO_CS_HOLD;
	}

	zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);

	/* Dummy generic FIFO entry */
	zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, 0x0);

	/* Manually start the generic FIFO command */
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
			zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
			GQSPI_CFG_START_GEN_FIFO_MASK);

	timeout = jiffies + msecs_to_jiffies(1000);

	/* Wait until the generic FIFO command is empty */
	do {
		statusreg = zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST);

		if ((statusreg & GQSPI_ISR_GENFIFOEMPTY_MASK) &&
			(statusreg & GQSPI_ISR_TXEMPTY_MASK))
			break;
		cpu_relax();
	} while (!time_after_eq(jiffies, timeout));

	if (time_after_eq(jiffies, timeout))
		dev_err(xqspi->dev, "Chip select timed out\n");
}

/**
 * zynqmp_qspi_setup_transfer -	Configure QSPI controller for specified
 *				transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	Always 0
 *
 * Note:
 *	If the requested frequency is not an exact match with what can be
 *	obtained using the pre-scalar value, the driver sets the clock
 *	frequency which is lower than the requested frequency (maximum lower)
 *	for the transfer.
 *
 *	If the requested frequency is higher or lower than that is supported
 *	by the QSPI controller the driver will set the highest or lowest
 *	frequency supported by controller.
 */
static int zynqmp_qspi_setup_transfer(struct spi_device *qspi,
				      struct spi_transfer *transfer)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(qspi->master);
	ulong clk_rate;
	u32 config_reg, req_hz, baud_rate_val = 0;

	if (transfer)
		req_hz = transfer->speed_hz;
	else
		req_hz = qspi->max_speed_hz;

	if (xqspi->speed_hz != req_hz) {
		/* Set the clock frequency */
		/* If req_hz == 0, default to lowest speed */
		clk_rate = clk_get_rate(xqspi->refclk);

		while ((baud_rate_val < GQSPI_BAUD_DIV_MAX) &&
		       (clk_rate /
			(GQSPI_BAUD_DIV_SHIFT << baud_rate_val)) > req_hz)
			baud_rate_val++;

		config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);

		/* Set the QSPI clock phase and clock polarity */
		config_reg &= (~GQSPI_CFG_CLK_PHA_MASK) &
			(~GQSPI_CFG_CLK_POL_MASK);

		if (qspi->mode & SPI_CPHA)
			config_reg |= GQSPI_CFG_CLK_PHA_MASK;
		if (qspi->mode & SPI_CPOL)
			config_reg |= GQSPI_CFG_CLK_POL_MASK;
		config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
		config_reg |= (baud_rate_val << GQSPI_CFG_BAUD_RATE_DIV_SHIFT);
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
		xqspi->speed_hz = clk_rate / (GQSPI_BAUD_DIV_SHIFT <<
				baud_rate_val);
		zynqmp_qspi_set_tapdelay(xqspi, baud_rate_val);
	}

	return 0;
}

/**
 * zynqmp_qspi_setup -	Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer,
 * baud rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success; error value otherwise.
 */
static int zynqmp_qspi_setup(struct spi_device *qspi)
{
	if (qspi->master->busy)
		return -EBUSY;
	return 0;
}

/**
 * zynqmp_qspi_filltxfifo -	Fills the TX FIFO as long as there is room in
 *				the FIFO or the bytes required to be
 *				transmitted.
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @size:	Number of bytes to be copied from TX buffer to TX FIFO
 */
static void zynqmp_qspi_filltxfifo(struct zynqmp_qspi *xqspi, int size)
{
	u32 count = 0, intermediate;

	while ((xqspi->bytes_to_transfer > 0) && (count < size)) {
		memcpy(&intermediate, xqspi->txbuf, 4);
		zynqmp_gqspi_write(xqspi, GQSPI_TXD_OFST, intermediate);

		if (xqspi->bytes_to_transfer >= 4) {
			xqspi->txbuf += 4;
			xqspi->bytes_to_transfer -= 4;
		} else {
			xqspi->txbuf += xqspi->bytes_to_transfer;
			xqspi->bytes_to_transfer = 0;
		}
		count++;
	}
}

/**
 * zynqmp_qspi_readrxfifo -	Fills the RX FIFO as long as there is room in
 *				the FIFO.
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @size:	Number of bytes to be copied from RX buffer to RX FIFO
 */
static void zynqmp_qspi_readrxfifo(struct zynqmp_qspi *xqspi, u32 size)
{
	ulong data;
	int count = 0;

	while ((count < size) && (xqspi->bytes_to_receive > 0)) {
		if (xqspi->bytes_to_receive >= 4) {
			(*(u32 *) xqspi->rxbuf) =
				zynqmp_gqspi_read(xqspi, GQSPI_RXD_OFST);
			xqspi->rxbuf += 4;
			xqspi->bytes_to_receive -= 4;
			count += 4;
		} else {
			data = zynqmp_gqspi_read(xqspi, GQSPI_RXD_OFST);
			count += xqspi->bytes_to_receive;
			zynqmp_qspi_copy_read_data(xqspi, data,
						   xqspi->bytes_to_receive);
			xqspi->bytes_to_receive = 0;
		}
	}
}

/**
 * zynqmp_qspi_preparedummy -	Prepares the dummy entry
 *
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @transfer:	It is a pointer to the structure containing transfer data.
 * @genfifoentry:	genfifoentry is pointer to the variable in which
 *			GENFIFO	mask is returned to calling function
 */
static void zynqmp_qspi_preparedummy(struct zynqmp_qspi *xqspi,
					struct spi_transfer *transfer,
					u32 *genfifoentry)
{
	/* For dummy Tx and Rx are NULL */
	*genfifoentry &= ~(GQSPI_GENFIFO_TX | GQSPI_GENFIFO_RX);

	/* SPI mode */
	*genfifoentry &= ~GQSPI_GENFIFO_MODE_QUADSPI;
	if (xqspi->rx_bus_width == GQSPI_RX_BUS_WIDTH_QUAD ||
			xqspi->tx_bus_width == GQSPI_TX_BUS_WIDTH_QUAD)
		*genfifoentry |= GQSPI_GENFIFO_MODE_QUADSPI;
	else if (xqspi->rx_bus_width == GQSPI_RX_BUS_WIDTH_DUAL ||
			xqspi->tx_bus_width == GQSPI_TX_BUS_WIDTH_DUAL)
		*genfifoentry |= GQSPI_GENFIFO_MODE_DUALSPI;
	else
		*genfifoentry |= GQSPI_GENFIFO_MODE_SPI;

	/* Immediate data */
	*genfifoentry &= ~GQSPI_GENFIFO_IMM_DATA_MASK;

	if (transfer->dummy)
		*genfifoentry |= transfer->dummy;
}

/**
 * zynqmp_process_dma_irq -	Handler for DMA done interrupt of QSPI
 *				controller
 * @xqspi:	zynqmp_qspi instance pointer
 *
 * This function handles DMA interrupt only.
 */
static void zynqmp_process_dma_irq(struct zynqmp_qspi *xqspi)
{
	u32 config_reg, genfifoentry;

	dma_unmap_single(xqspi->dev, xqspi->dma_addr,
				xqspi->dma_rx_bytes, DMA_FROM_DEVICE);
	xqspi->rxbuf += xqspi->dma_rx_bytes;
	xqspi->bytes_to_receive -= xqspi->dma_rx_bytes;
	xqspi->dma_rx_bytes = 0;

	/* Disabling the DMA interrupts */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_DIS_OFST,
					GQSPI_QSPIDMA_DST_I_EN_DONE_MASK);

	if (xqspi->bytes_to_receive > 0) {
		/* Switch to IO mode,for remaining bytes to receive */
		config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
		config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);

		/* Initiate the transfer of remaining bytes */
		genfifoentry = xqspi->genfifoentry;
		genfifoentry |= xqspi->bytes_to_receive;
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);

		/* Dummy generic FIFO entry */
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, 0x0);

		/* Manual start */
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
			(zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
			GQSPI_CFG_START_GEN_FIFO_MASK));

		/* Enable the RX interrupts for IO mode */
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				GQSPI_IER_GENFIFOEMPTY_MASK |
				GQSPI_IER_RXNEMPTY_MASK |
				GQSPI_IER_RXEMPTY_MASK);
	}
}

/**
 * zynqmp_qspi_irq -	Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO
 * and fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED when interrupt is handled
 *		IRQ_NONE otherwise.
 */
static irqreturn_t zynqmp_qspi_irq(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	int ret = IRQ_NONE;
	u32 status, mask, dma_status = 0;

	status = zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST);
	zynqmp_gqspi_write(xqspi, GQSPI_ISR_OFST, status);
	mask = (status & ~(zynqmp_gqspi_read(xqspi, GQSPI_IMASK_OFST)));

	/* Read and clear DMA status */
	if (xqspi->mode == GQSPI_MODE_DMA) {
		dma_status =
			zynqmp_gqspi_read(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST);
		zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST,
								dma_status);
	}

	if (mask & GQSPI_ISR_TXNOT_FULL_MASK) {
		zynqmp_qspi_filltxfifo(xqspi, GQSPI_TX_FIFO_FILL);
		ret = IRQ_HANDLED;
	}

	if (dma_status & GQSPI_QSPIDMA_DST_I_STS_DONE_MASK) {
		zynqmp_process_dma_irq(xqspi);
		ret = IRQ_HANDLED;
	} else if ((mask & GQSPI_IER_RXNEMPTY_MASK)) {
		zynqmp_qspi_readrxfifo(xqspi, GQSPI_RX_FIFO_FILL);
		ret = IRQ_HANDLED;
	}
	if (!(mask & GQSPI_IER_RXEMPTY_MASK) &&
		(mask & GQSPI_IER_GENFIFOEMPTY_MASK)) {
		zynqmp_qspi_readrxfifo(xqspi, GQSPI_RX_FIFO_FILL);
		ret = IRQ_HANDLED;
	}

	if ((xqspi->bytes_to_receive == 0) && (xqspi->bytes_to_transfer == 0)
			&& ((status & GQSPI_IRQ_MASK) == GQSPI_IRQ_MASK)) {
		zynqmp_disable_intr(xqspi);
		xqspi->isinstr = false;
		spi_finalize_current_transfer(master);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/**
 * zynqmp_qspi_selectspimode -	Selects SPI mode - x1 or x2 or x4.
 * @xqspi:	xqspi is a pointer to the GQSPI instance
 * @spimode:	spimode - SPI or DUAL or QUAD.
 * Return:	Mask to set desired SPI mode in GENFIFO entry.
 */
static inline u32 zynqmp_qspi_selectspimode(struct zynqmp_qspi *xqspi,
						u8 spimode)
{
	u32 mask = 0;

	switch (spimode) {
	case GQSPI_SELECT_MODE_DUALSPI:
		mask = GQSPI_GENFIFO_MODE_DUALSPI;
		break;
	case GQSPI_SELECT_MODE_QUADSPI:
		mask = GQSPI_GENFIFO_MODE_QUADSPI;
		break;
	case GQSPI_SELECT_MODE_SPI:
		mask = GQSPI_GENFIFO_MODE_SPI;
		break;
	default:
		dev_warn(xqspi->dev, "Invalid SPI mode\n");
	}

	return mask;
}

/**
 * zynq_qspi_setuprxdma -	This function sets up the RX DMA operation
 * @xqspi:	xqspi is a pointer to the GQSPI instance.
 */
static void zynq_qspi_setuprxdma(struct zynqmp_qspi *xqspi)
{
	u32 rx_bytes, rx_rem, config_reg;
	dma_addr_t addr;
	u64 dma_align =  (u64)(uintptr_t)xqspi->rxbuf;

	if (((xqspi->bytes_to_receive < 8) || (xqspi->io_mode)) ||
		((dma_align & GQSPI_DMA_UNALIGN) != 0x0) ||
		is_vmalloc_addr(xqspi->rxbuf)) {
		/* Setting to IO mode */
		config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
		config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
		xqspi->mode = GQSPI_MODE_IO;
		xqspi->dma_rx_bytes = 0;
		return;
	}

	rx_rem = xqspi->bytes_to_receive % 4;
	rx_bytes = (xqspi->bytes_to_receive - rx_rem);

	addr = dma_map_single(xqspi->dev, (void *)xqspi->rxbuf,
						rx_bytes, DMA_FROM_DEVICE);
	if (dma_mapping_error(xqspi->dev, addr)) {
		dev_err(xqspi->dev, "ERR:rxdma:memory not mapped\n");
		return;
	}

	xqspi->dma_rx_bytes = rx_bytes;
	xqspi->dma_addr = addr;
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_ADDR_OFST,
				(u32)(addr & 0xffffffff));
	addr = ((addr >> 16) >> 16);
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_ADDR_MSB_OFST,
				((u32)addr) & 0xfff);

	/* Enabling the DMA mode */
	config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	config_reg |= GQSPI_CFG_MODE_EN_DMA_MASK;
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);

	/* Switch to DMA mode */
	xqspi->mode = GQSPI_MODE_DMA;

	/* Write the number of bytes to transfer */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_SIZE_OFST, rx_bytes);
}

/**
 * zynqmp_qspi_txrxsetup -	This function checks the TX/RX buffers in
 *				the transfer and sets up the GENFIFO entries,
 *				TX FIFO as required.
 * @xqspi:	xqspi is a pointer to the GQSPI instance.
 * @transfer:	It is a pointer to the structure containing transfer data.
 * @genfifoentry:	genfifoentry is pointer to the variable in which
 *			GENFIFO	mask is returned to calling function
 */
static void zynqmp_qspi_txrxsetup(struct zynqmp_qspi *xqspi,
				  struct spi_transfer *transfer,
				  u32 *genfifoentry)
{
	u32 config_reg;

	/* Transmit */
	if ((xqspi->txbuf != NULL) && (xqspi->rxbuf == NULL)) {
		/* Setup data to be TXed */
		*genfifoentry &= ~GQSPI_GENFIFO_RX;
		*genfifoentry |= GQSPI_GENFIFO_DATA_XFER;
		*genfifoentry |= GQSPI_GENFIFO_TX;
		*genfifoentry |=
			zynqmp_qspi_selectspimode(xqspi, transfer->tx_nbits);
		xqspi->bytes_to_transfer = transfer->len - (transfer->dummy/8);
		if (xqspi->mode == GQSPI_MODE_DMA) {
			config_reg = zynqmp_gqspi_read(xqspi,
							GQSPI_CONFIG_OFST);
			config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
			zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
								config_reg);
			xqspi->mode = GQSPI_MODE_IO;
		}
		zynqmp_qspi_filltxfifo(xqspi, GQSPI_TXD_DEPTH);
		/* Discard RX data */
		xqspi->bytes_to_receive = 0;
	} else if ((xqspi->txbuf == NULL) && (xqspi->rxbuf != NULL)) {
		/* Receive */

		/* TX auto fill */
		*genfifoentry &= ~GQSPI_GENFIFO_TX;
		/* Setup RX */
		*genfifoentry |= GQSPI_GENFIFO_DATA_XFER;
		*genfifoentry |= GQSPI_GENFIFO_RX;
		*genfifoentry |=
			zynqmp_qspi_selectspimode(xqspi, transfer->rx_nbits);
		xqspi->bytes_to_transfer = 0;
		xqspi->bytes_to_receive = transfer->len;
		zynq_qspi_setuprxdma(xqspi);
	}
}

/**
 * zynqmp_qspi_start_transfer -	Initiates the QSPI transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the QSPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int zynqmp_qspi_start_transfer(struct spi_master *master,
				      struct spi_device *qspi,
				      struct spi_transfer *transfer)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	u32 genfifoentry = 0x0, transfer_len;

	xqspi->txbuf = transfer->tx_buf;
	xqspi->rxbuf = transfer->rx_buf;

	zynqmp_qspi_setup_transfer(qspi, transfer);

	genfifoentry |= xqspi->genfifocs;
	genfifoentry |= xqspi->genfifobus;

	if (!xqspi->isinstr && (master->flags & SPI_MASTER_DATA_STRIPE)) {
		if (transfer->stripe)
			genfifoentry |= GQSPI_GENFIFO_STRIPE;
	}
	zynqmp_qspi_txrxsetup(xqspi, transfer, &genfifoentry);

	if (xqspi->mode == GQSPI_MODE_DMA)
		transfer_len = xqspi->dma_rx_bytes;
	else
		transfer_len = transfer->len - (transfer->dummy/8);

	xqspi->genfifoentry = genfifoentry;
	if ((transfer_len) < GQSPI_GENFIFO_IMM_DATA_MASK) {
		genfifoentry &= ~GQSPI_GENFIFO_IMM_DATA_MASK;
		genfifoentry |= transfer_len;
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);
		if (transfer->dummy || transfer->tx_nbits >= 1) {
			zynqmp_qspi_preparedummy(xqspi, transfer,
					&genfifoentry);
			zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST,
					genfifoentry);
		}
	} else {
		int tempcount = transfer_len;
		u32 exponent = 8;	/* 2^8 = 256 */
		u8 imm_data = tempcount & 0xFF;

		tempcount &= ~(tempcount & 0xFF);
		/* Immediate entry */
		if (tempcount != 0) {
			/* Exponent entries */
			genfifoentry |= GQSPI_GENFIFO_EXP;
			while (tempcount != 0) {
				if (tempcount & GQSPI_GENFIFO_EXP_START) {
					genfifoentry &=
					    ~GQSPI_GENFIFO_IMM_DATA_MASK;
					genfifoentry |= exponent;
					zynqmp_gqspi_write(xqspi,
							   GQSPI_GEN_FIFO_OFST,
							   genfifoentry);
				}
				tempcount = tempcount >> 1;
				exponent++;
			}
		}
		if (imm_data != 0) {
			genfifoentry &= ~GQSPI_GENFIFO_EXP;
			genfifoentry &= ~GQSPI_GENFIFO_IMM_DATA_MASK;
			if (imm_data % 4 != 0) {
				if (((imm_data + 4 - (imm_data % 4)) & 0xFF) == 0x00)
					imm_data = 0xFF;
				else
					imm_data = imm_data + 4 - (imm_data % 4);
			}
			genfifoentry |= (u8) (imm_data & 0xFF);
			zynqmp_gqspi_write(xqspi,
					   GQSPI_GEN_FIFO_OFST, genfifoentry);
		}
	}

	if ((xqspi->mode == GQSPI_MODE_IO) &&
			(xqspi->rxbuf != NULL)) {
		/* Dummy generic FIFO entry */
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, 0x0);
	}

	/* Since we are using manual mode */
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
			   GQSPI_CFG_START_GEN_FIFO_MASK);

	if (xqspi->txbuf != NULL)
		/* Enable interrupts for TX */
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
					GQSPI_IER_GENFIFOEMPTY_MASK |
					GQSPI_IER_TXNOT_FULL_MASK);

	if (xqspi->rxbuf != NULL) {
		/* Enable interrupts for RX */
		if (xqspi->mode == GQSPI_MODE_DMA) {
			/* Enable DMA interrupts */
			zynqmp_gqspi_write(xqspi,
					GQSPI_QSPIDMA_DST_I_EN_OFST,
					GQSPI_QSPIDMA_DST_I_EN_DONE_MASK);
		} else {
			zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
					GQSPI_IER_GENFIFOEMPTY_MASK |
					GQSPI_IER_RXNEMPTY_MASK);
		}
	}

	return transfer->len;
}

/**
 * zynqmp_qspi_suspend -	Suspend method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function stops the QSPI driver queue and disables the QSPI controller
 *
 * Return:	Always 0
 */
static int __maybe_unused zynqmp_qspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);

	spi_master_suspend(master);

	zynqmp_unprepare_transfer_hardware(master);

	return 0;
}

/**
 * zynqmp_qspi_resume -	Resume method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the QSPI driver queue and initializes the QSPI
 * controller
 *
 * Return:	0 on success; error value otherwise
 */
static int __maybe_unused zynqmp_qspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = clk_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xqspi->pclk);
		return ret;
	}

	zynqmp_qspi_init_hw(xqspi);
	spi_master_resume(master);

	clk_disable(xqspi->refclk);
	clk_disable(xqspi->pclk);

	return 0;
}

/**
 * zynqmp_runtime_suspend - Runtime suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the clocks
 *
 * Return:	Always 0
 */
static int __maybe_unused zynqmp_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	clk_disable(xqspi->refclk);
	clk_disable(xqspi->pclk);

	return 0;
}

/**
 * zynqmp_runtime_resume - Runtime resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function enables the clocks
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused zynqmp_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xqspi->pclk);
		return ret;
	}

	return 0;
}

static int __maybe_unused zynqmp_runtime_idle(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	u32 value;

	value = zynqmp_gqspi_read(xqspi, GQSPI_EN_OFST);
	if (value)
		return -EBUSY;

	return 0;
}

static const struct dev_pm_ops zynqmp_qspi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(zynqmp_runtime_suspend,
			   zynqmp_runtime_resume, zynqmp_runtime_idle)
	SET_SYSTEM_SLEEP_PM_OPS(zynqmp_qspi_suspend, zynqmp_qspi_resume)
};

static const struct qspi_platform_data versal_qspi_def = {
	.quirks = QSPI_QUIRK_HAS_TAPDELAY,
};

static const struct of_device_id zynqmp_qspi_of_match[] = {
	{ .compatible = "xlnx,zynqmp-qspi-1.0"},
	{ .compatible = "xlnx,versal-qspi-1.0", .data = &versal_qspi_def },
	{ /* End of table */ }
};

/**
 * zynqmp_qspi_probe -	Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success; error value otherwise
 */
static int zynqmp_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_master *master;
	struct zynqmp_qspi *xqspi;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *nc;
	const struct of_device_id *match;
	u32 num_cs;
	u32 rx_bus_width;
	u32 tx_bus_width;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	master = spi_alloc_master(&pdev->dev, sizeof(*xqspi));
	if (!master)
		return -ENOMEM;

	xqspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	match = of_match_node(zynqmp_qspi_of_match, pdev->dev.of_node);
	if (match) {
		const struct qspi_platform_data *p_data = match->data;

		if (p_data && (p_data->quirks & QSPI_QUIRK_HAS_TAPDELAY))
			xqspi->has_tapdelay = true;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xqspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xqspi->regs)) {
		ret = PTR_ERR(xqspi->regs);
		goto remove_master;
	}

	xqspi->dev = dev;
	xqspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xqspi->pclk)) {
		dev_err(dev, "pclk clock not found.\n");
		ret = PTR_ERR(xqspi->pclk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Unable to enable APB clock.\n");
		goto remove_master;
	}

	xqspi->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->refclk)) {
		dev_err(dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xqspi->refclk);
		goto clk_dis_pclk;
	}

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Unable to enable device clock.\n");
		goto clk_dis_pclk;
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (of_property_read_bool(pdev->dev.of_node, "has-io-mode"))
		xqspi->io_mode = true;

	/* QSPI controller initializations */
	zynqmp_qspi_init_hw(xqspi);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq <= 0) {
		ret = -ENXIO;
		dev_err(dev, "irq resource not found\n");
		goto clk_dis_all;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynqmp_qspi_irq,
			       0, pdev->name, master);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(dev, "request_irq failed\n");
		goto clk_dis_all;
	}

	xqspi->rx_bus_width = GQSPI_RX_BUS_WIDTH_SINGLE;
	for_each_available_child_of_node(pdev->dev.of_node, nc) {
		ret = of_property_read_u32(nc, "spi-rx-bus-width",
					&rx_bus_width);
		if (!ret) {
			xqspi->rx_bus_width = rx_bus_width;
			break;
		}
	}
	if (ret)
		dev_err(dev, "rx bus width not found\n");

	xqspi->tx_bus_width = GQSPI_TX_BUS_WIDTH_SINGLE;
	for_each_available_child_of_node(pdev->dev.of_node, nc) {
		ret = of_property_read_u32(nc, "spi-tx-bus-width",
					&tx_bus_width);
		if (!ret) {
			xqspi->tx_bus_width = tx_bus_width;
			break;
		}
	}
	if (ret)
		dev_err(dev, "tx bus width not found\n");

	ret = of_property_read_u32(pdev->dev.of_node, "num-cs", &num_cs);
	if (ret < 0)
		master->num_chipselect = GQSPI_DEFAULT_NUM_CS;
	else
		master->num_chipselect = num_cs;

	dma_set_mask(&pdev->dev, DMA_BIT_MASK(44));
	master->setup = zynqmp_qspi_setup;
	master->set_cs = zynqmp_qspi_chipselect;
	master->transfer_one = zynqmp_qspi_start_transfer;
	master->prepare_transfer_hardware = zynqmp_prepare_transfer_hardware;
	master->unprepare_transfer_hardware =
					zynqmp_unprepare_transfer_hardware;
	master->max_speed_hz = clk_get_rate(xqspi->refclk) / 2;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD |
			    SPI_TX_DUAL | SPI_TX_QUAD;
	xqspi->speed_hz = master->max_speed_hz;
	master->auto_runtime_pm = true;

	if (master->dev.parent == NULL)
		master->dev.parent = &master->dev;

	ret = spi_register_master(master);
	if (ret)
		goto clk_dis_all;

	return 0;

clk_dis_all:
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(xqspi->refclk);
clk_dis_pclk:
	clk_disable_unprepare(xqspi->pclk);
remove_master:
	spi_master_put(master);

	return ret;
}

/**
 * zynqmp_qspi_remove -	Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 Always
 */
static int zynqmp_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x0);
	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	spi_unregister_master(master);

	return 0;
}

MODULE_DEVICE_TABLE(of, zynqmp_qspi_of_match);

static struct platform_driver zynqmp_qspi_driver = {
	.probe = zynqmp_qspi_probe,
	.remove = zynqmp_qspi_remove,
	.driver = {
		.name = "zynqmp-qspi",
		.of_match_table = zynqmp_qspi_of_match,
		.pm = &zynqmp_qspi_dev_pm_ops,
	},
};

module_platform_driver(zynqmp_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynqmp QSPI driver");
MODULE_LICENSE("GPL");

/*
 * Xilinx Zynq Quad-SPI (QSPI) controller driver (master mode only)
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 *
 * based on Xilinx Zynq SPI Driver (spi-zynq.c)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/*
 * Name of this driver
 */
#define DRIVER_NAME			"zynq-qspi"

/*
 * Register offset definitions
 */
#define ZYNQ_QSPI_CONFIG_OFFSET		0x00 /* Configuration  Register, RW */
#define ZYNQ_QSPI_STATUS_OFFSET		0x04 /* Interrupt Status Register, RO */
#define ZYNQ_QSPI_IEN_OFFSET		0x08 /* Interrupt Enable Register, WO */
#define ZYNQ_QSPI_IDIS_OFFSET		0x0C /* Interrupt Disable Reg, WO */
#define ZYNQ_QSPI_IMASK_OFFSET		0x10 /* Interrupt Enabled Mask Reg,RO */
#define ZYNQ_QSPI_ENABLE_OFFSET		0x14 /* Enable/Disable Register, RW */
#define ZYNQ_QSPI_DELAY_OFFSET		0x18 /* Delay Register, RW */
#define ZYNQ_QSPI_TXD_00_00_OFFSET	0x1C /* Transmit 4-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_01_OFFSET	0x80 /* Transmit 1-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_10_OFFSET	0x84 /* Transmit 2-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_11_OFFSET	0x88 /* Transmit 3-byte inst, WO */
#define ZYNQ_QSPI_RXD_OFFSET		0x20 /* Data Receive Register, RO */
#define ZYNQ_QSPI_SIC_OFFSET		0x24 /* Slave Idle Count Register, RW */
#define ZYNQ_QSPI_TX_THRESH_OFFSET	0x28 /* TX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_RX_THRESH_OFFSET	0x2C /* RX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_GPIO_OFFSET		0x30 /* GPIO Register, RW */
#define ZYNQ_QSPI_LINEAR_CFG_OFFSET	0xA0 /* Linear Adapter Config Ref, RW */
#define ZYNQ_QSPI_MOD_ID_OFFSET		0xFC /* Module ID Register, RO */

/*
 * QSPI Configuration Register bit Masks
 *
 * This register contains various control bits that effect the operation
 * of the QSPI controller
 */
#define ZYNQ_QSPI_CONFIG_IFMODE_MASK	0x80000000 /* Flash Memory Interface */
#define ZYNQ_QSPI_CONFIG_MANSRT_MASK	0x00010000 /* Manual TX Start */
#define ZYNQ_QSPI_CONFIG_MANSRTEN_MASK	0x00008000 /* Enable Manual TX Mode */
#define ZYNQ_QSPI_CONFIG_SSFORCE_MASK	0x00004000 /* Manual Chip Select */
#define ZYNQ_QSPI_CONFIG_BDRATE_MASK	0x00000038 /* Baud Rate Divisor Mask */
#define ZYNQ_QSPI_CONFIG_CPHA_MASK	0x00000004 /* Clock Phase Control */
#define ZYNQ_QSPI_CONFIG_CPOL_MASK	0x00000002 /* Clock Polarity Control */
#define ZYNQ_QSPI_CONFIG_SSCTRL_MASK	0x00003C00 /* Slave Select Mask */
#define ZYNQ_QSPI_CONFIG_FWIDTH_MASK	0x000000C0 /* FIFO width */
#define ZYNQ_QSPI_CONFIG_MSTREN_MASK	0x00000001 /* Master Mode */

/*
 * QSPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define ZYNQ_QSPI_IXR_TXNFULL_MASK	0x00000004 /* QSPI TX FIFO Overflow */
#define ZYNQ_QSPI_IXR_TXFULL_MASK	0x00000008 /* QSPI TX FIFO is full */
#define ZYNQ_QSPI_IXR_RXNEMTY_MASK	0x00000010 /* QSPI RX FIFO Not Empty */
#define ZYNQ_QSPI_IXR_ALL_MASK		(ZYNQ_QSPI_IXR_TXNFULL_MASK | \
					ZYNQ_QSPI_IXR_RXNEMTY_MASK)

/*
 * QSPI Enable Register bit Masks
 *
 * This register is used to enable or disable the QSPI controller
 */
#define ZYNQ_QSPI_ENABLE_ENABLE_MASK	0x00000001 /* QSPI Enable Bit Mask */

/*
 * QSPI Linear Configuration Register
 *
 * It is named Linear Configuration but it controls other modes when not in
 * linear mode also.
 */
#define ZYNQ_QSPI_LCFG_TWO_MEM_MASK	0x40000000 /* LQSPI Two memories Mask */
#define ZYNQ_QSPI_LCFG_SEP_BUS_MASK	0x20000000 /* LQSPI Separate bus Mask */
#define ZYNQ_QSPI_LCFG_U_PAGE_MASK	0x10000000 /* LQSPI Upper Page Mask */

#define ZYNQ_QSPI_LCFG_DUMMY_SHIFT	8

#define ZYNQ_QSPI_FAST_READ_QOUT_CODE	0x6B	/* read instruction code */
#define ZYNQ_QSPI_FIFO_DEPTH		63	/* FIFO depth in words */
#define ZYNQ_QSPI_RX_THRESHOLD		32	/* Rx FIFO threshold level */

/*
 * The modebits configurable by the driver to make the SPI support different
 * data formats
 */
#define MODEBITS			(SPI_CPOL | SPI_CPHA)

/*
 * Definitions for the status of queue
 */
#define ZYNQ_QSPI_QUEUE_STOPPED		0
#define ZYNQ_QSPI_QUEUE_RUNNING		1

/*
 * Definitions of the flash commands
 */
/* Flash opcodes in ascending order */
/* Write status register */
#define	ZYNQ_QSPI_FLASH_OPCODE_WRSR		0x01
/* Page program */
#define	ZYNQ_QSPI_FLASH_OPCODE_PP		0x02
/* Normal read data bytes */
#define	ZYNQ_QSPI_FLASH_OPCODE_NORM_READ	0x03
/* Write disable */
#define	ZYNQ_QSPI_FLASH_OPCODE_WRDS		0x04
/* Read status register 1 */
#define	ZYNQ_QSPI_FLASH_OPCODE_RDSR1		0x05
/* Write enable */
#define	ZYNQ_QSPI_FLASH_OPCODE_WREN		0x06
/* Bank Register Read */
#define	ZYNQ_QSPI_FLASH_OPCODE_BRRD		0x16
/* Bank Register Write */
#define	ZYNQ_QSPI_FLASH_OPCODE_BRWR		0x17
/* Micron - Bank Reg Read */
#define	ZYNQ_QSPI_FLASH_OPCODE_EXTADRD		0xC8
/* Micron - Bank Reg Write */
#define	ZYNQ_QSPI_FLASH_OPCODE_EXTADWR		0xC5
/* Fast read data bytes */
#define	ZYNQ_QSPI_FLASH_OPCODE_FAST_READ	0x0B
/* Erase 4KiB block */
#define	ZYNQ_QSPI_FLASH_OPCODE_BE_4K		0x20
/* Read status register 2 */
#define	ZYNQ_QSPI_FLASH_OPCODE_RDSR2		0x35
/* Read flag status register */
#define	ZYNQ_QSPI_FLASH_OPCODE_RDFSR		0x70
/* Dual read data bytes */
#define	ZYNQ_QSPI_FLASH_OPCODE_DUAL_READ	0x3B
/* Erase 32KiB block */
#define	ZYNQ_QSPI_FLASH_OPCODE_BE_32K		0x52
/* Quad read data bytes */
#define	ZYNQ_QSPI_FLASH_OPCODE_QUAD_READ	0x6B
/* Erase suspend */
#define	ZYNQ_QSPI_FLASH_OPCODE_ERASE_SUS	0x75
/* Erase resume */
#define	ZYNQ_QSPI_FLASH_OPCODE_ERASE_RES	0x7A
/* Read JEDEC ID */
#define	ZYNQ_QSPI_FLASH_OPCODE_RDID		0x9F
/* Erase whole flash block */
#define	ZYNQ_QSPI_FLASH_OPCODE_BE		0xC7
/* Sector erase (usually 64KB) */
#define	ZYNQ_QSPI_FLASH_OPCODE_SE		0xD8
/* Quad page program */
#define ZYNQ_QSPI_FLASH_OPCODE_QPP		0x32

/*
 * Macros for the QSPI controller read/write
 */
#define zynq_qspi_read(addr)		readl_relaxed(addr)
#define zynq_qspi_write(addr, val)	writel_relaxed((val), (addr))

/**
 * struct zynq_qspi - Defines qspi driver instance
 * @workqueue:		Queue of all the transfers
 * @work:		Information about current transfer
 * @queue:		Head of the queue
 * @queue_state:	Queue status
 * @regs:		Virtual address of the QSPI controller registers
 * @devclk:		Pointer to the peripheral clock
 * @aperclk:		Pointer to the APER clock
 * @irq:		IRQ number
 * @speed_hz:		Current QSPI bus clock speed in Hz
 * @trans_queue_lock:	Lock used for accessing transfer queue
 * @config_reg_lock:	Lock used for accessing configuration register
 * @txbuf:		Pointer	to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @dev_busy:		Device busy flag
 * @done:		Transfer complete status
 * @is_inst:		Flag to indicate the first message in a Transfer request
 * @is_dual:		Flag to indicate whether dual flash memories are used
 */
struct zynq_qspi {
	struct workqueue_struct *workqueue;
	struct work_struct work;
	struct list_head queue;
	u8 queue_state;
	void __iomem *regs;
	struct clk *devclk;
	struct clk *aperclk;
	int irq;
	u32 speed_hz;
	spinlock_t trans_queue_lock;
	spinlock_t config_reg_lock;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u8 dev_busy;
	struct completion done;
	bool is_inst;
	u32 is_dual;
};

/**
 * struct zynq_qspi_inst_format - Defines qspi flash instruction format
 * @opcode:		Operational code of instruction
 * @inst_size:		Size of the instruction including address bytes
 * @offset:		Register address where instruction has to be written
 */
struct zynq_qspi_inst_format {
	u8 opcode;
	u8 inst_size;
	u8 offset;
};

/*
 * List of all the QSPI instructions and its format
 */
static struct zynq_qspi_inst_format flash_inst[] = {
	{ ZYNQ_QSPI_FLASH_OPCODE_WREN, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_WRDS, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_RDSR1, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_RDSR2, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_WRSR, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_RDFSR, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_PP, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_SE, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_BE_32K, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_BE_4K, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_BE, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_ERASE_SUS, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_ERASE_RES, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_RDID, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_NORM_READ, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_FAST_READ, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_DUAL_READ, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_QUAD_READ, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_BRRD, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_BRWR, 2, ZYNQ_QSPI_TXD_00_10_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_EXTADRD, 1, ZYNQ_QSPI_TXD_00_01_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_EXTADWR, 2, ZYNQ_QSPI_TXD_00_10_OFFSET },
	{ ZYNQ_QSPI_FLASH_OPCODE_QPP, 4, ZYNQ_QSPI_TXD_00_00_OFFSET },
	/* Add all the instructions supported by the flash device */
};

/**
 * zynq_qspi_init_hw - Initialize the hardware
 * @xqspi:	Pointer to the zynq_qspi structure
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Master mode
 *	- Baud rate divisor is set to 2
 *	- Threshold value for TX FIFO not full interrupt is set to 1
 *	- Flash memory interface mode enabled
 *	- Size of the word to be transferred as 8 bit
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual slave select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the size of the word to be transferred as 32 bit
 *	- Set the little endian mode of TX FIFO and
 *	- Enable the QSPI controller
 */
static void zynq_qspi_init_hw(struct zynq_qspi *xqspi)
{
	u32 config_reg;

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_ENABLE_OFFSET, 0);
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_IDIS_OFFSET, 0x7F);

	/* Disable linear mode as the boot loader may have used it */
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_LINEAR_CFG_OFFSET, 0);

	/* Clear the RX FIFO */
	while (zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_STATUS_OFFSET) &
			ZYNQ_QSPI_IXR_RXNEMTY_MASK)
		zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_RXD_OFFSET);

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_STATUS_OFFSET , 0x7F);
	config_reg = zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET);
	config_reg &= ~(ZYNQ_QSPI_CONFIG_MSTREN_MASK |
			ZYNQ_QSPI_CONFIG_CPOL_MASK |
			ZYNQ_QSPI_CONFIG_CPHA_MASK |
			ZYNQ_QSPI_CONFIG_BDRATE_MASK |
			ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
			ZYNQ_QSPI_CONFIG_MANSRTEN_MASK |
			ZYNQ_QSPI_CONFIG_MANSRT_MASK);
	config_reg |= (ZYNQ_QSPI_CONFIG_MSTREN_MASK |
			ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
			ZYNQ_QSPI_CONFIG_FWIDTH_MASK |
			ZYNQ_QSPI_CONFIG_IFMODE_MASK);
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_RX_THRESH_OFFSET,
				ZYNQ_QSPI_RX_THRESHOLD);
	if (xqspi->is_dual)
		/* Enable two memories on seperate buses */
		zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_LINEAR_CFG_OFFSET,
			(ZYNQ_QSPI_LCFG_TWO_MEM_MASK |
			 ZYNQ_QSPI_LCFG_SEP_BUS_MASK |
			 (1 << ZYNQ_QSPI_LCFG_DUMMY_SHIFT) |
			 ZYNQ_QSPI_FAST_READ_QOUT_CODE));
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
	/* Enable two memories on shared bus */
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_LINEAR_CFG_OFFSET,
		 (ZYNQ_QSPI_LCFG_TWO_MEM_MASK |
		 (1 << ZYNQ_QSPI_LCFG_DUMMY_SHIFT) |
		 ZYNQ_QSPI_FAST_READ_QOUT_CODE));
#endif
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_ENABLE_OFFSET,
			ZYNQ_QSPI_ENABLE_ENABLE_MASK);
}

/**
 * zynq_qspi_copy_read_data - Copy data to RX buffer
 * @xqspi:	Pointer to the zynq_qspi structure
 * @data:	The 32 bit variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 */
static void zynq_qspi_copy_read_data(struct zynq_qspi *xqspi, u32 data, u8 size)
{
	if (xqspi->rxbuf) {
		data >>= (4 - size) * 8;
		data = le32_to_cpu(data);
		memcpy((u8 *)xqspi->rxbuf, &data, size);
		xqspi->rxbuf += size;
	}
	xqspi->bytes_to_receive -= size;
	if (xqspi->bytes_to_receive < 0)
		xqspi->bytes_to_receive = 0;
}

/**
 * zynq_qspi_copy_write_data - Copy data from TX buffer
 * @xqspi:	Pointer to the zynq_qspi structure
 * @data:	Pointer to the 32 bit variable where data is to be copied
 * @size:	Number of bytes to be copied from TX buffer to data
 */
static void zynq_qspi_copy_write_data(struct zynq_qspi *xqspi, u32 *data,
				      u8 size)
{

	if (xqspi->txbuf) {
		switch (size) {
		case 1:
			*data = *((u8 *)xqspi->txbuf);
			xqspi->txbuf += 1;
			*data |= 0xFFFFFF00;
			break;
		case 2:
			*data = *((u16 *)xqspi->txbuf);
			xqspi->txbuf += 2;
			*data |= 0xFFFF0000;
			break;
		case 3:
			*data = *((u16 *)xqspi->txbuf);
			xqspi->txbuf += 2;
			*data |= (*((u8 *)xqspi->txbuf) << 16);
			xqspi->txbuf += 1;
			*data |= 0xFF000000;
			break;
		case 4:
			*data = *((u32 *)xqspi->txbuf);
			xqspi->txbuf += 4;
			break;
		default:
			/* This will never execute */
			break;
		}
	} else {
		*data = 0;
	}

	xqspi->bytes_to_transfer -= size;
	if (xqspi->bytes_to_transfer < 0)
		xqspi->bytes_to_transfer = 0;
}

/**
 * zynq_qspi_chipselect - Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_on:	Select(1) or deselect (0) the chip select line
 */
static void zynq_qspi_chipselect(struct spi_device *qspi, int is_on)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg;
	unsigned long flags;

	spin_lock_irqsave(&xqspi->config_reg_lock, flags);

	config_reg = zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET);

	if (is_on) {
		/* Select the slave */
		config_reg &= ~ZYNQ_QSPI_CONFIG_SSCTRL_MASK;
		config_reg |= (((~(0x0001 << qspi->chip_select)) << 10) &
				ZYNQ_QSPI_CONFIG_SSCTRL_MASK);
	} else {
		/* Deselect the slave */
		config_reg |= ZYNQ_QSPI_CONFIG_SSCTRL_MASK;
	}

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	spin_unlock_irqrestore(&xqspi->config_reg_lock, flags);
}

/**
 * zynq_qspi_setup_transfer - Configure QSPI controller for specified transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides information
 *		about next transfer setup parameters
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	0 on success and -EINVAL on invalid input parameter
 *
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value, the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the QSPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static int zynq_qspi_setup_transfer(struct spi_device *qspi,
		struct spi_transfer *transfer)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg;
	u32 req_hz;
	u32 baud_rate_val = 0;
	unsigned long flags;
	int update_baud = 0;

	req_hz = (transfer) ? transfer->speed_hz : qspi->max_speed_hz;

	/* Set the clock frequency */
	/* If req_hz == 0, default to lowest speed */
	if (xqspi->speed_hz != req_hz) {
		while ((baud_rate_val < 7)  &&
			(clk_get_rate(xqspi->devclk) / (2 << baud_rate_val)) >
			req_hz)
				baud_rate_val++;
		xqspi->speed_hz = req_hz;
		update_baud = 1;
	}

	spin_lock_irqsave(&xqspi->config_reg_lock, flags);

	config_reg = zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET);

	/* Set the QSPI clock phase and clock polarity */
	config_reg &= (~ZYNQ_QSPI_CONFIG_CPHA_MASK) &
				(~ZYNQ_QSPI_CONFIG_CPOL_MASK);
	if (qspi->mode & SPI_CPHA)
		config_reg |= ZYNQ_QSPI_CONFIG_CPHA_MASK;
	if (qspi->mode & SPI_CPOL)
		config_reg |= ZYNQ_QSPI_CONFIG_CPOL_MASK;

	if (update_baud) {
		config_reg &= 0xFFFFFFC7;
		config_reg |= (baud_rate_val << 3);
	}

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	spin_unlock_irqrestore(&xqspi->config_reg_lock, flags);

	dev_dbg(&qspi->dev, "%s, mode %d, %u bits/w, %u clock speed\n",
		__func__, qspi->mode & MODEBITS, qspi->bits_per_word,
		xqspi->speed_hz);

	return 0;
}

/**
 * zynq_qspi_setup - Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer, baud
 * rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_setup(struct spi_device *qspi)
{
	if (qspi->bits_per_word && qspi->bits_per_word != 8) {
		dev_err(&qspi->dev, "%s, unsupported bits per word %u\n",
			__func__, qspi->bits_per_word);
		return -EINVAL;
	}

	return zynq_qspi_setup_transfer(qspi, NULL);
}

/**
 * zynq_qspi_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xqspi:	Pointer to the zynq_qspi structure
 * @size:	Size of the fifo to be filled
 */
static void zynq_qspi_fill_tx_fifo(struct zynq_qspi *xqspi, u32 size)
{
	u32 fifocount;

	for (fifocount = 0; (fifocount < size) &&
			(xqspi->bytes_to_transfer >= 4); fifocount++) {
		if (xqspi->txbuf) {
			zynq_qspi_write(xqspi->regs +
					ZYNQ_QSPI_TXD_00_00_OFFSET,
						*((u32 *)xqspi->txbuf));
			xqspi->txbuf += 4;
		} else {
			zynq_qspi_write(xqspi->regs +
					ZYNQ_QSPI_TXD_00_00_OFFSET, 0x00);
		}
		xqspi->bytes_to_transfer -= 4;
		if (xqspi->bytes_to_transfer < 0)
			xqspi->bytes_to_transfer = 0;
	}
}

/**
 * zynq_qspi_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED always
 */
static irqreturn_t zynq_qspi_irq(int irq, void *dev_id)
{
	struct zynq_qspi *xqspi = dev_id;
	u32 intr_status;
	u8 offset[3] =	{ZYNQ_QSPI_TXD_00_01_OFFSET, ZYNQ_QSPI_TXD_00_10_OFFSET,
		ZYNQ_QSPI_TXD_00_11_OFFSET};
	u32 rxcount;
	u32 rxindex = 0;

	intr_status = zynq_qspi_read(xqspi->regs + ZYNQ_QSPI_STATUS_OFFSET);
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_STATUS_OFFSET , intr_status);
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_IDIS_OFFSET,
			ZYNQ_QSPI_IXR_ALL_MASK);

	if ((intr_status & ZYNQ_QSPI_IXR_TXNFULL_MASK) ||
		   (intr_status & ZYNQ_QSPI_IXR_RXNEMTY_MASK)) {
		/* This bit is set when Tx FIFO has < THRESHOLD entries. We have
		   the THRESHOLD value set to 1, so this bit indicates Tx FIFO
		   is empty */
		u32 data;

		rxcount = xqspi->bytes_to_receive - xqspi->bytes_to_transfer;
		rxcount = (rxcount % 4) ? ((rxcount/4) + 1) : (rxcount/4);

		/* Read out the data from the RX FIFO */
		while ((rxindex < rxcount) &&
				(rxindex < ZYNQ_QSPI_RX_THRESHOLD)) {

			if (xqspi->bytes_to_receive < 4 && !xqspi->is_dual) {
				data = zynq_qspi_read(xqspi->regs +
						ZYNQ_QSPI_RXD_OFFSET);
				zynq_qspi_copy_read_data(xqspi, data,
					xqspi->bytes_to_receive);
			} else {
				if (xqspi->rxbuf) {
					(*(u32 *)xqspi->rxbuf) =
					zynq_qspi_read(xqspi->regs +
						ZYNQ_QSPI_RXD_OFFSET);
					xqspi->rxbuf += 4;
				} else {
					data = zynq_qspi_read(xqspi->regs +
							ZYNQ_QSPI_RXD_OFFSET);
				}
				xqspi->bytes_to_receive -= 4;
				if (xqspi->bytes_to_receive < 0)
					xqspi->bytes_to_receive = 0;
			}
			rxindex++;
		}

		if (xqspi->bytes_to_transfer) {
			if (xqspi->bytes_to_transfer >= 4) {
				/* There is more data to send */
				zynq_qspi_fill_tx_fifo(xqspi,
						ZYNQ_QSPI_RX_THRESHOLD);
			} else {
				int tmp;
				tmp = xqspi->bytes_to_transfer;
				zynq_qspi_copy_write_data(xqspi, &data,
					xqspi->bytes_to_transfer);
				if (xqspi->is_dual)
					zynq_qspi_write(xqspi->regs +
						ZYNQ_QSPI_TXD_00_00_OFFSET,
							data);
				else
					zynq_qspi_write(xqspi->regs +
						offset[tmp - 1], data);
			}
			zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_IEN_OFFSET,
					ZYNQ_QSPI_IXR_ALL_MASK);
		} else {
			/* If transfer and receive is completed then only send
			 * complete signal */
			if (xqspi->bytes_to_receive) {
				/* There is still some data to be received.
				   Enable Rx not empty interrupt */
				zynq_qspi_write(xqspi->regs +
						ZYNQ_QSPI_IEN_OFFSET,
						ZYNQ_QSPI_IXR_ALL_MASK);
			} else {
				zynq_qspi_write(xqspi->regs +
						ZYNQ_QSPI_IDIS_OFFSET,
						ZYNQ_QSPI_IXR_ALL_MASK);
				complete(&xqspi->done);
			}
		}
	}

	return IRQ_HANDLED;
}

/**
 * zynq_qspi_start_transfer - Initiates the QSPI transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the QSPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int zynq_qspi_start_transfer(struct spi_device *qspi,
			struct spi_transfer *transfer)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 data = 0;
	u8 instruction = 0;
	u8 index;
	struct zynq_qspi_inst_format *curr_inst;

	xqspi->txbuf = transfer->tx_buf;
	xqspi->rxbuf = transfer->rx_buf;
	xqspi->bytes_to_transfer = transfer->len;
	xqspi->bytes_to_receive = transfer->len;

	if (xqspi->txbuf)
		instruction = *(u8 *)xqspi->txbuf;

	reinit_completion(&xqspi->done);
	if (instruction && xqspi->is_inst) {
		for (index = 0 ; index < ARRAY_SIZE(flash_inst); index++)
			if (instruction == flash_inst[index].opcode)
				break;

		/* Instruction might have already been transmitted. This is a
		 * 'data only' transfer */
		if (index == ARRAY_SIZE(flash_inst))
			goto xfer_data;

		curr_inst = &flash_inst[index];

		/* Get the instruction */
		data = 0;
		zynq_qspi_copy_write_data(xqspi, &data,
					curr_inst->inst_size);

		/* Write the instruction to LSB of the FIFO. The core is
		 * designed such that it is not necessary to check whether the
		 * write FIFO is full before writing. However, write would be
		 * delayed if the user tries to write when write FIFO is full
		 */
		zynq_qspi_write(xqspi->regs + curr_inst->offset, data);
		goto xfer_start;
	}

xfer_data:
	/* In case of Fast, Dual and Quad reads, transmit the instruction first.
	 * Address and dummy byte will be transmitted in interrupt handler,
	 * after instruction is transmitted */
	if (((xqspi->is_inst == 0) && (xqspi->bytes_to_transfer >= 4)) ||
	     ((xqspi->bytes_to_transfer >= 4) &&
	      (instruction != ZYNQ_QSPI_FLASH_OPCODE_FAST_READ) &&
	      (instruction != ZYNQ_QSPI_FLASH_OPCODE_DUAL_READ) &&
	      (instruction != ZYNQ_QSPI_FLASH_OPCODE_QUAD_READ)))
		zynq_qspi_fill_tx_fifo(xqspi, ZYNQ_QSPI_FIFO_DEPTH);

xfer_start:
	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_IEN_OFFSET,
			ZYNQ_QSPI_IXR_ALL_MASK);

	wait_for_completion(&xqspi->done);

	return (transfer->len) - (xqspi->bytes_to_transfer);
}

/**
 * zynq_qspi_work_queue - Get the request from queue to perform transfers
 * @work:	Pointer to the work_struct structure
 */
static void zynq_qspi_work_queue(struct work_struct *work)
{
	struct zynq_qspi *xqspi = container_of(work, struct zynq_qspi, work);
	unsigned long flags;
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
	u32 lqspi_cfg_reg;
#endif

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	xqspi->dev_busy = 1;

	/* Check if list is empty or queue is stoped */
	if (list_empty(&xqspi->queue) ||
		xqspi->queue_state == ZYNQ_QSPI_QUEUE_STOPPED) {
		xqspi->dev_busy = 0;
		spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
		return;
	}

	/* Keep requesting transfer till list is empty */
	while (!list_empty(&xqspi->queue)) {
		struct spi_message *msg;
		struct spi_device *qspi;
		struct spi_transfer *transfer = NULL;
		unsigned cs_change = 1;
		int status = 0;

		msg = container_of(xqspi->queue.next, struct spi_message,
					queue);
		list_del_init(&msg->queue);
		spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
		qspi = msg->spi;

#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
		lqspi_cfg_reg = zynq_qspi_read(xqspi->regs +
					ZYNQ_QSPI_LINEAR_CFG_OFFSET);
		if (qspi->master->flags & SPI_MASTER_U_PAGE)
			lqspi_cfg_reg |= ZYNQ_QSPI_LCFG_U_PAGE_MASK;
		else
			lqspi_cfg_reg &= ~ZYNQ_QSPI_LCFG_U_PAGE_MASK;
		zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_LINEAR_CFG_OFFSET,
			      lqspi_cfg_reg);
#endif

		list_for_each_entry(transfer, &msg->transfers, transfer_list) {
			if (transfer->speed_hz) {
				status = zynq_qspi_setup_transfer(qspi,
								  transfer);
				if (status < 0)
					break;
			}

			/* Select the chip if required */
			if (cs_change) {
				zynq_qspi_chipselect(qspi, 1);
				xqspi->is_inst = 1;
			}

			cs_change = transfer->cs_change;

			if (!transfer->tx_buf && !transfer->rx_buf &&
				transfer->len) {
				status = -EINVAL;
				break;
			}

			/* Request the transfer */
			if (transfer->len) {
				status = zynq_qspi_start_transfer(qspi,
								  transfer);
				xqspi->is_inst = 0;
			}

			if (status != transfer->len) {
				if (status > 0)
					status = -EMSGSIZE;
				break;
			}
			msg->actual_length += status;
			status = 0;

			if (transfer->delay_usecs)
				udelay(transfer->delay_usecs);

			if (cs_change)
				/* Deselect the chip */
				zynq_qspi_chipselect(qspi, 0);

			if (transfer->transfer_list.next == &msg->transfers)
				break;
		}

		msg->status = status;
		msg->complete(msg->context);

		zynq_qspi_setup_transfer(qspi, NULL);

		if (!(status == 0 && cs_change))
			zynq_qspi_chipselect(qspi, 0);

		spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	}
	xqspi->dev_busy = 0;
	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
}

/**
 * zynq_qspi_transfer - Add a new transfer request at the tail of work queue
 * @qspi:	Pointer to the spi_device structure
 * @message:	Pointer to the spi_transfer structure which provides information
 *		about next transfer parameters
 *
 * Return:	0 on success, -EINVAL on invalid input parameter and
 *		-ESHUTDOWN if queue is stopped by module unload function
 */
static int zynq_qspi_transfer(struct spi_device *qspi,
			    struct spi_message *message)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	struct spi_transfer *transfer;
	unsigned long flags;

	if (xqspi->queue_state == ZYNQ_QSPI_QUEUE_STOPPED)
		return -ESHUTDOWN;

	message->actual_length = 0;
	message->status = -EINPROGRESS;

	/* Check each transfer's parameters */
	list_for_each_entry(transfer, &message->transfers, transfer_list) {
		if (!transfer->tx_buf && !transfer->rx_buf && transfer->len)
			return -EINVAL;
		/* We only support 8-bit transfers */
		if (transfer->bits_per_word && transfer->bits_per_word != 8)
			return -EINVAL;
	}

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	list_add_tail(&message->queue, &xqspi->queue);
	if (!xqspi->dev_busy)
		queue_work(xqspi->workqueue, &xqspi->work);
	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);

	return 0;
}

/**
 * zynq_qspi_start_queue - Starts the queue of the QSPI driver
 * @xqspi:	Pointer to the zynq_qspi structure
 *
 * Return:	0 on success and -EBUSY if queue is already running or device is
 *		busy
 */
static inline int zynq_qspi_start_queue(struct zynq_qspi *xqspi)
{
	unsigned long flags;

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);

	if (xqspi->queue_state == ZYNQ_QSPI_QUEUE_RUNNING || xqspi->dev_busy) {
		spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
		return -EBUSY;
	}

	xqspi->queue_state = ZYNQ_QSPI_QUEUE_RUNNING;
	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);

	return 0;
}

/**
 * zynq_qspi_stop_queue - Stops the queue of the QSPI driver
 * @xqspi:	Pointer to the zynq_qspi structure
 *
 * This function waits till queue is empty and then stops the queue.
 * Maximum time out is set to 5 seconds.
 *
 * Return:	0 on success and -EBUSY if queue is not empty or device is busy
 */
static inline int zynq_qspi_stop_queue(struct zynq_qspi *xqspi)
{
	unsigned long flags;
	unsigned limit = 500;
	int ret = 0;

	if (xqspi->queue_state != ZYNQ_QSPI_QUEUE_RUNNING)
		return ret;

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);

	while ((!list_empty(&xqspi->queue) || xqspi->dev_busy) && limit--) {
		spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
		msleep(10);
		spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	}

	if (!list_empty(&xqspi->queue) || xqspi->dev_busy)
		ret = -EBUSY;

	if (ret == 0)
		xqspi->queue_state = ZYNQ_QSPI_QUEUE_STOPPED;

	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);

	return ret;
}

/**
 * zynq_qspi_destroy_queue - Destroys the queue of the QSPI driver
 * @xqspi:	Pointer to the zynq_qspi structure
 *
 * Return:	0 on success and error value on failure
 */
static inline int zynq_qspi_destroy_queue(struct zynq_qspi *xqspi)
{
	int ret;

	ret = zynq_qspi_stop_queue(xqspi);
	if (ret != 0)
		return ret;

	destroy_workqueue(xqspi->workqueue);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/**
 * zynq_qspi_suspend - Suspend method for the QSPI driver
 * @_dev:	Address of the platform_device structure
 *
 * This function stops the QSPI driver queue and disables the QSPI controller
 *
 * Return:	0 on success and error value on error
 */
static int zynq_qspi_suspend(struct device *_dev)
{
	struct platform_device *pdev = container_of(_dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = zynq_qspi_stop_queue(xqspi);
	if (ret != 0)
		return ret;

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_ENABLE_OFFSET, 0);

	clk_disable(xqspi->devclk);
	clk_disable(xqspi->aperclk);

	dev_dbg(&pdev->dev, "suspend succeeded\n");
	return 0;
}

/**
 * zynq_qspi_resume - Resume method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the QSPI driver queue and initializes the QSPI controller
 *
 * Return:	0 on success and error value on error
 */
static int zynq_qspi_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = clk_enable(xqspi->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(xqspi->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xqspi->aperclk);
		return ret;
	}

	zynq_qspi_init_hw(xqspi);

	ret = zynq_qspi_start_queue(xqspi);
	if (ret != 0) {
		dev_err(&pdev->dev, "problem starting queue (%d)\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "resume succeeded\n");
	return 0;
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(zynq_qspi_dev_pm_ops, zynq_qspi_suspend,
			 zynq_qspi_resume);

/**
 * zynq_qspi_probe - Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_master *master;
	struct zynq_qspi *xqspi;
	struct resource *res;

	master = spi_alloc_master(&pdev->dev, sizeof(*xqspi));
	if (master == NULL)
		return -ENOMEM;

	xqspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xqspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xqspi->regs)) {
		ret = PTR_ERR(xqspi->regs);
		goto remove_master;
	}

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq < 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "irq resource not found\n");
		goto remove_master;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynq_qspi_irq,
			       0, pdev->name, xqspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto remove_master;
	}

	if (of_property_read_u32(pdev->dev.of_node, "is-dual", &xqspi->is_dual))
		dev_warn(&pdev->dev, "couldn't determine configuration info "
			 "about dual memories. defaulting to single memory\n");

	xqspi->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(xqspi->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		ret = PTR_ERR(xqspi->aperclk);
		goto remove_master;
	}

	xqspi->devclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xqspi->devclk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->aperclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->devclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_aper;
	}

	/* QSPI controller initializations */
	zynq_qspi_init_hw(xqspi);

	init_completion(&xqspi->done);

	ret = of_property_read_u32(pdev->dev.of_node, "num-chip-select",
				   (u32 *)&master->num_chipselect);
	if (ret < 0) {
		dev_err(&pdev->dev, "couldn't determine num-chip-select\n");
		goto clk_dis_all;
	}

	master->setup = zynq_qspi_setup;
	master->transfer = zynq_qspi_transfer;
	master->flags = SPI_MASTER_QUAD_MODE;

	xqspi->speed_hz = clk_get_rate(xqspi->devclk) / 2;

	xqspi->dev_busy = 0;

	INIT_LIST_HEAD(&xqspi->queue);
	spin_lock_init(&xqspi->trans_queue_lock);
	spin_lock_init(&xqspi->config_reg_lock);

	xqspi->queue_state = ZYNQ_QSPI_QUEUE_STOPPED;
	xqspi->dev_busy = 0;

	INIT_WORK(&xqspi->work, zynq_qspi_work_queue);
	xqspi->workqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!xqspi->workqueue) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "problem initializing queue\n");
		goto clk_dis_all;
	}

	ret = zynq_qspi_start_queue(xqspi);
	if (ret != 0) {
		dev_err(&pdev->dev, "problem starting queue\n");
		goto remove_queue;
	}

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto remove_queue;
	}

	dev_info(&pdev->dev, "at 0x%08X mapped to 0x%08X, irq=%d\n", res->start,
		 (u32 __force)xqspi->regs, xqspi->irq);

	return ret;

remove_queue:
	(void)zynq_qspi_destroy_queue(xqspi);
clk_dis_all:
	clk_disable_unprepare(xqspi->devclk);
clk_dis_aper:
	clk_disable_unprepare(xqspi->aperclk);
remove_master:
	spi_master_put(master);
	return ret;
}

/**
 * zynq_qspi_remove - Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = zynq_qspi_destroy_queue(xqspi);
	if (ret != 0)
		return ret;

	zynq_qspi_write(xqspi->regs + ZYNQ_QSPI_ENABLE_OFFSET, 0);

	clk_disable_unprepare(xqspi->devclk);
	clk_disable_unprepare(xqspi->aperclk);

	spi_unregister_master(master);

	dev_dbg(&pdev->dev, "remove succeeded\n");
	return 0;
}

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:" DRIVER_NAME);

static struct of_device_id zynq_qspi_of_match[] = {
	{ .compatible = "xlnx,zynq-qspi-1.0", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, zynq_qspi_of_match);

/*
 * zynq_qspi_driver - This structure defines the QSPI platform driver
 */
static struct platform_driver zynq_qspi_driver = {
	.probe	= zynq_qspi_probe,
	.remove	= zynq_qspi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = zynq_qspi_of_match,
		.pm = &zynq_qspi_dev_pm_ops,
	},
};

module_platform_driver(zynq_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq QSPI driver");
MODULE_LICENSE("GPL");

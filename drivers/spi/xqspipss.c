/*
 *
 * Xilinx PSS Quad-SPI (QSPI) controller driver (master mode only)
 *
 * (c) 2009 Xilinx, Inc.
 *
 * based on Xilinx PSS SPI Driver (xspipss.c)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/xilinx_devices.h>


/*
 * Name of this driver
 */
#define DRIVER_NAME			"Xilinx_PSS_QSPI"

/*
 * Register offset definitions
 */
#define XQSPIPSS_CONFIG_OFFSET		0x00 /* Configuration  Register, RW */
#define XQSPIPSS_STATUS_OFFSET 		0x04 /* Interrupt Status Register, RO */
#define XQSPIPSS_IEN_OFFSET		0x08 /* Interrupt Enable Register, WO */
#define XQSPIPSS_IDIS_OFFSET		0x0C /* Interrupt Disable Reg, WO */
#define XQSPIPSS_IMASK_OFFSET		0x10 /* Interrupt Enabled Mask Reg,RO */
#define XQSPIPSS_ENABLE_OFFSET		0x14 /* Enable/Disable Register, RW */
#define XQSPIPSS_DELAY_OFFSET 		0x18 /* Delay Register, RW */
#define XQSPIPSS_TXD_00_00_OFFSET	0x1C /* Transmit 4-byte inst, WO */
#define XQSPIPSS_TXD_00_01_OFFSET	0x80 /* Transmit 1-byte inst, WO */
#define XQSPIPSS_TXD_00_10_OFFSET	0x84 /* Transmit 2-byte inst, WO */
#define XQSPIPSS_TXD_00_11_OFFSET	0x88 /* Transmit 3-byte inst, WO */
#define XQSPIPSS_RXD_OFFSET		0x20 /* Data Receive Register, RO */
#define XQSPIPSS_SIC_OFFSET		0x24 /* Slave Idle Count Register, RW */
#define XQSPIPSS_TX_THRESH_OFFSET	0x28 /* TX FIFO Watermark Reg, RW */
#define XQSPIPSS_RX_THRESH_OFFSET	0x2C /* RX FIFO Watermark Reg, RW */
#define XQSPIPSS_GPIO_OFFSET		0x30 /* GPIO Register, RW */
#define XQSPIPSS_MOD_ID_OFFSET		0xFC /* Module ID Register, RO */

/*
 * QSPI Configuration Register bit Masks
 *
 * This register contains various control bits that effect the operation
 * of the QSPI controller
 */
#define XQSPIPSS_CONFIG_MANSRT_MASK	0x00010000 /* Manual TX Start */
#define XQSPIPSS_CONFIG_CPHA_MASK	0x00000004 /* Clock Phase Control */
#define XQSPIPSS_CONFIG_CPOL_MASK	0x00000002 /* Clock Polarity Control */
#define XQSPIPSS_CONFIG_SSCTRL_MASK	0x00003C00 /* Slave Select Mask */

/*
 * QSPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define XQSPIPSS_IXR_MODF_MASK		0x00000002 /* QSPI Mode Fault */
#define XQSPIPSS_IXR_TXNFULL_MASK	0x00000004 /* QSPI TX FIFO Overflow */
#define XQSPIPSS_IXR_TXFULL_MASK	0x00000008 /* QSPI TX FIFO is full */
#define XQSPIPSS_IXR_RXNEMTY_MASK	0x00000010 /* QSPI RX FIFO Not Empty */
#define XQSPIPSS_IXR_ALL_MASK		(XQSPIPSS_IXR_TXNFULL_MASK | \
					 XQSPIPSS_IXR_MODF_MASK)

/*
 * QSPI Enable Register bit Masks
 *
 * This register is used to enable or disable the QSPI controller
 */
#define XQSPIPSS_ENABLE_ENABLE_MASK	0x00000001 /* QSPI Enable Bit Mask */

/*
 * The modebits configurable by the driver to make the SPI support different
 * data formats
 */
#define MODEBITS			(SPI_CPOL | SPI_CPHA)

/*
 * Definitions for the status of queue
 */
#define XQSPIPSS_QUEUE_STOPPED		0
#define XQSPIPSS_QUEUE_RUNNING		1

/*
 * Definitions of the flash commands
 */
/* Flash opcodes in ascending order */
#define	XQSPIPSS_FLASH_OPCODE_WRSR	0x01	/* Write status register */
#define	XQSPIPSS_FLASH_OPCODE_PP	0x02	/* Page program */
#define	XQSPIPSS_FLASH_OPCODE_NORM_READ	0x03	/* Normal read data bytes */
#define	XQSPIPSS_FLASH_OPCODE_WRDS	0x04	/* Write disable */
#define	XQSPIPSS_FLASH_OPCODE_RDSR1	0x05	/* Read status register 1 */
#define	XQSPIPSS_FLASH_OPCODE_WREN	0x06	/* Write enable */
#define	XQSPIPSS_FLASH_OPCODE_FAST_READ	0x0B	/* Fast read data bytes */
#define	XQSPIPSS_FLASH_OPCODE_BE_4K	0x20	/* Erase 4KiB block */
#define	XQSPIPSS_FLASH_OPCODE_RDSR2	0x35	/* Read status register 2 */
#define	XQSPIPSS_FLASH_OPCODE_DUAL_READ	0x3B	/* Dual read data bytes */
#define	XQSPIPSS_FLASH_OPCODE_BE_32K	0x52	/* Erase 32KiB block */
#define	XQSPIPSS_FLASH_OPCODE_QUAD_READ	0x6B	/* Quad read data bytes */
#define	XQSPIPSS_FLASH_OPCODE_ERASE_SUS	0x75	/* Erase suspend */
#define	XQSPIPSS_FLASH_OPCODE_ERASE_RES	0x7A	/* Erase resume */
#define	XQSPIPSS_FLASH_OPCODE_RDID	0x9F	/* Read JEDEC ID */
#define	XQSPIPSS_FLASH_OPCODE_BE	0xC7	/* Erase whole flash block */
#define	XQSPIPSS_FLASH_OPCODE_SE	0xD8	/* Sector erase (usually 64KB)*/

/*
 * Macros for the QSPI controller read/write
 */
#define xqspipss_read(addr)		__raw_readl(addr)
#define xqspipss_write(addr, val)	__raw_writel((val), (addr))

/**
 * struct xqspipss - Defines qspi driver instance
 * @workqueue:		Queue of all the transfers
 * @work:		Information about current transfer
 * @queue:		Head of the queue
 * @queue_state:	Queue status
 * @regs:		Virtual address of the QSPI controller registers
 * @input_clk_hz:	Input clock frequency of the QSPI controller in Hz
 * @irq:		IRQ number
 * @speed_hz:		Current QSPI bus clock speed in Hz
 * @trans_queue_lock:	Lock used for accessing transfer queue
 * @config_reg_lock:	Lock used for accessing configuration register
 * @txbuf: 		Pointer	to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @dev_busy:		Device busy flag
 * @done:		Transfer complete status
 * @curr_inst:		Current executing instruction format
 * @inst_response:	Responce to the instruction or data
 **/
struct xqspipss {
	struct workqueue_struct *workqueue;
	struct work_struct work;
	struct list_head queue;
	u8 queue_state;
	void __iomem *regs;
	u32 input_clk_hz;
	u32 irq;
	u32 speed_hz;
	spinlock_t trans_queue_lock;
	spinlock_t config_reg_lock;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u8 dev_busy;
	struct completion done;
	struct xqspipss_inst_format *curr_inst;
	u8 inst_response;
};

/**
 * struct xqspipss_inst_format - Defines qspi flash instruction format
 * @opcode:		Operational code of instruction
 * @inst_size:		Size of the instruction including address bytes
 * @offset:		Register address where instruction has to be written
 **/
struct xqspipss_inst_format {
	u8 opcode;
	u8 inst_size;
	u8 offset;
};

/*
 * List of all the QSPI instructions and its format
 */
static struct xqspipss_inst_format __devinitdata flash_inst[] = {
	{ XQSPIPSS_FLASH_OPCODE_WREN, 1, XQSPIPSS_TXD_00_01_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_WRDS, 1, XQSPIPSS_TXD_00_01_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_RDSR1, 2, XQSPIPSS_TXD_00_10_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_RDSR2, 2, XQSPIPSS_TXD_00_10_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_WRSR, 3, XQSPIPSS_TXD_00_11_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_PP, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_SE, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_BE_32K, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_BE_4K, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_BE, 1, XQSPIPSS_TXD_00_01_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_ERASE_SUS, 1, XQSPIPSS_TXD_00_01_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_ERASE_RES, 1, XQSPIPSS_TXD_00_01_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_RDID, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_NORM_READ, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_FAST_READ, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_DUAL_READ, 4, XQSPIPSS_TXD_00_00_OFFSET },
	{ XQSPIPSS_FLASH_OPCODE_QUAD_READ, 4, XQSPIPSS_TXD_00_00_OFFSET },
	/* Add all the instructions supported by the flash device */
};

/**
 * xqspipss_init_hw - Initialize the hardware
 * @regs_base:		Base address of QSPI controller
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Master mode
 * 	- Baud rate divisor is set to 2
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
 **/
static void xqspipss_init_hw(void __iomem *regs_base)
{
	u32 config_reg;

	xqspipss_write(regs_base + XQSPIPSS_ENABLE_OFFSET,
		~XQSPIPSS_ENABLE_ENABLE_MASK);
	xqspipss_write(regs_base + XQSPIPSS_IDIS_OFFSET, 0x7F);

	/* Clear the RX FIFO */
	while (xqspipss_read(regs_base + XQSPIPSS_STATUS_OFFSET) &
			XQSPIPSS_IXR_RXNEMTY_MASK)
		xqspipss_read(regs_base + XQSPIPSS_RXD_OFFSET);

	xqspipss_write(regs_base + XQSPIPSS_STATUS_OFFSET , 0x7F);
	config_reg = xqspipss_read(regs_base + XQSPIPSS_CONFIG_OFFSET);
	config_reg &= 0xFBFFFFFF; /* Set little endian mode of TX FIFO */
	config_reg |= 0x8000FCC1;
	xqspipss_write(regs_base + XQSPIPSS_CONFIG_OFFSET, config_reg);
	xqspipss_write(regs_base + XQSPIPSS_ENABLE_OFFSET,
			XQSPIPSS_ENABLE_ENABLE_MASK);
}

/**
 * xqspipss_copy_read_data - Copy data to RX buffer
 * @xqspi:	Pointer to the xqspipss structure
 * @data:	The 32 bit variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 **/
static void xqspipss_copy_read_data(struct xqspipss *xqspi, u32 data, u8 size)
{
	u8 byte3;

	if (xqspi->rxbuf) {
		switch (size) {
		case 1:
			*((u8 *)xqspi->rxbuf) = data;
			xqspi->rxbuf += 1;
			break;
		case 2:
			*((u16 *)xqspi->rxbuf) = data;
			xqspi->rxbuf += 2;
			break;
		case 3:
			*((u16 *)xqspi->rxbuf) = data;
			xqspi->rxbuf += 2;
			byte3 = (u8)data >> 16;
			*((u8 *)xqspi->rxbuf) = byte3;
			xqspi->rxbuf += 1;
			break;
		case 4:
			(*(u32 *)xqspi->rxbuf) = data;
			xqspi->rxbuf += 4;
			break;
		default:
			/* This will never execute */
			break;
		}
	}
	xqspi->bytes_to_receive -= size;
}

/**
 * xqspipss_copy_write_data - Copy data from TX buffer
 * @xqspi:	Pointer to the xqspipss structure
 * @data:	Pointer to the 32 bit variable where data is to be copied
 * @size:	Number of bytes to be copied from TX buffer to data
 **/
static void xqspipss_copy_write_data(struct xqspipss *xqspi, u32 *data, u8 size)
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
	} else
		*data = 0;

	xqspi->bytes_to_transfer -= size;
}

/**
 * xqspipss_chipselect - Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_on:	Select(1) or deselect (0) the chip select line
 **/
static void xqspipss_chipselect(struct spi_device *qspi, int is_on)
{
	struct xqspipss *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg;
	unsigned long flags;

	spin_lock_irqsave(&xqspi->config_reg_lock, flags);

	config_reg = xqspipss_read(xqspi->regs + XQSPIPSS_CONFIG_OFFSET);

	if (is_on) {
		/* Select the slave */
		config_reg &= ~XQSPIPSS_CONFIG_SSCTRL_MASK;
		config_reg |= (((~(0x0001 << qspi->chip_select)) << 10) &
				XQSPIPSS_CONFIG_SSCTRL_MASK);
	} else
		/* Deselect the slave */
		config_reg |= XQSPIPSS_CONFIG_SSCTRL_MASK;

	xqspipss_write(xqspi->regs + XQSPIPSS_CONFIG_OFFSET, config_reg);

	spin_unlock_irqrestore(&xqspi->config_reg_lock, flags);
}

/**
 * xqspipss_setup_transfer - Configure QSPI controller for specified transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides information
 *		about next transfer setup parameters
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * returns:	0 on success and -EINVAL on invalid input parameter
 *
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value, the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the QSPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 **/
static int xqspipss_setup_transfer(struct spi_device *qspi,
		struct spi_transfer *transfer)
{
	struct xqspipss *xqspi = spi_master_get_devdata(qspi->master);
	u8 bits_per_word;
	u32 config_reg;
	u32 req_hz;
	u32 baud_rate_val = 0;
	unsigned long flags;

	bits_per_word = (transfer) ?
			transfer->bits_per_word : qspi->bits_per_word;
	req_hz = (transfer) ? transfer->speed_hz : qspi->max_speed_hz;

	if (qspi->mode & ~MODEBITS) {
		dev_err(&qspi->dev, "%s, unsupported mode bits %x\n",
			__func__, qspi->mode & ~MODEBITS);
		return -EINVAL;
	}

	if (bits_per_word != 32) {
		bits_per_word = 32;
	}

	spin_lock_irqsave(&xqspi->config_reg_lock, flags);

	config_reg = xqspipss_read(xqspi->regs + XQSPIPSS_CONFIG_OFFSET);

	/* Set the QSPI clock phase and clock polarity */
	config_reg &= (~XQSPIPSS_CONFIG_CPHA_MASK) &
				(~XQSPIPSS_CONFIG_CPOL_MASK);
	if (qspi->mode & SPI_CPHA)
		config_reg |= XQSPIPSS_CONFIG_CPHA_MASK;
	if (qspi->mode & SPI_CPOL)
		config_reg |= XQSPIPSS_CONFIG_CPOL_MASK;

	/* Set the clock frequency */
	if (xqspi->speed_hz != req_hz) {
		baud_rate_val = 0;
		while ((baud_rate_val < 8)  &&
			(xqspi->input_clk_hz / (2 << baud_rate_val)) > req_hz) {
				baud_rate_val++;
		}
		config_reg &= 0xFFFFFFC7;
		config_reg |= (baud_rate_val << 3);
		xqspi->speed_hz = req_hz;
	}

	xqspipss_write(xqspi->regs + XQSPIPSS_CONFIG_OFFSET, config_reg);

	spin_unlock_irqrestore(&xqspi->config_reg_lock, flags);

	dev_dbg(&qspi->dev, "%s, mode %d, %u bits/w, %u clock speed\n",
		__func__, qspi->mode & MODEBITS, qspi->bits_per_word,
		xqspi->speed_hz);

	return 0;
}

/**
 * xqspipss_setup - Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer, baud
 * rate and divisor value to setup the requested qspi clock.
 *
 * returns:	0 on success and error value on failure
 **/
static int xqspipss_setup(struct spi_device *qspi)
{

	if (qspi->mode & SPI_LSB_FIRST)
		return -EINVAL;

	if (!qspi->max_speed_hz)
		return -EINVAL;

	if (!qspi->bits_per_word)
		qspi->bits_per_word = 32;

	return xqspipss_setup_transfer(qspi, NULL);
}

/**
 * xqspipss_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xqspi:	Pointer to the xqspipss structure
 **/
static void xqspipss_fill_tx_fifo(struct xqspipss *xqspi)
{
	u32 data = 0;

	while ((!(xqspipss_read(xqspi->regs + XQSPIPSS_STATUS_OFFSET) &
		XQSPIPSS_IXR_TXFULL_MASK)) && (xqspi->bytes_to_transfer > 0)) {
		if (xqspi->bytes_to_transfer < 4) {
			xqspipss_copy_write_data(xqspi, &data,
				xqspi->bytes_to_transfer);
		} else {
			xqspipss_copy_write_data(xqspi, &data, 4);
		}

		xqspipss_write(xqspi->regs + XQSPIPSS_TXD_00_00_OFFSET, data);
	}

	if (xqspi->bytes_to_transfer)
		xqspipss_write(xqspi->regs + XQSPIPSS_TX_THRESH_OFFSET, 127);
	else
		xqspipss_write(xqspi->regs + XQSPIPSS_TX_THRESH_OFFSET, 1);
}

/**
 * xqspipss_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty and Mode Fault interrupts only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 * On Mode Fault interrupt this function indicates that transfer is completed,
 * the SPI subsystem will identify the error as the remaining bytes to be
 * transferred is non-zero.
 *
 * returns:	IRQ_HANDLED always
 **/
static irqreturn_t xqspipss_irq(int irq, void *dev_id)
{
	struct xqspipss *xqspi = dev_id;
	u32 intr_status;

	intr_status = xqspipss_read(xqspi->regs + XQSPIPSS_STATUS_OFFSET);
	xqspipss_write(xqspi->regs + XQSPIPSS_STATUS_OFFSET , intr_status);
	xqspipss_write(xqspi->regs + XQSPIPSS_IDIS_OFFSET,
			XQSPIPSS_IXR_ALL_MASK);

	if (intr_status & XQSPIPSS_IXR_MODF_MASK) {
		/* Indicate that transfer is completed, the SPI subsystem will
		 * identify the error as the remaining bytes to be
		 * transferred is non-zero */
		complete(&xqspi->done);
	} else if (intr_status & XQSPIPSS_IXR_TXNFULL_MASK) {
		/* This bit is set when Tx FIFO has < THRESHOLD entries. We have
		   the THRESHOLD value set to 1, so this bit indicates Tx FIFO
		   is empty */
		u32 config_reg;

		/* Read out the data from the RX FIFO */
		while (xqspipss_read(xqspi->regs + XQSPIPSS_STATUS_OFFSET) &
			XQSPIPSS_IXR_RXNEMTY_MASK) {
			u32 data;

			data = xqspipss_read(xqspi->regs + XQSPIPSS_RXD_OFFSET);

			if ((xqspi->inst_response) &&
				(!((xqspi->curr_inst->opcode ==
					XQSPIPSS_FLASH_OPCODE_RDSR1) ||
				(xqspi->curr_inst->opcode ==
					XQSPIPSS_FLASH_OPCODE_RDSR2)))) {
				xqspi->inst_response = 0;
				xqspipss_copy_read_data(xqspi, data,
					xqspi->curr_inst->inst_size);
			} else if (xqspi->bytes_to_receive < 4)
				xqspipss_copy_read_data(xqspi, data,
					xqspi->bytes_to_receive);
			else
				xqspipss_copy_read_data(xqspi, data, 4);
		}

		if (xqspi->bytes_to_transfer) {
			/* There is more data to send */
			xqspipss_fill_tx_fifo(xqspi);

			xqspipss_write(xqspi->regs + XQSPIPSS_IEN_OFFSET,
					XQSPIPSS_IXR_ALL_MASK);

			spin_lock(&xqspi->config_reg_lock);
			config_reg = xqspipss_read(xqspi->regs +
						XQSPIPSS_CONFIG_OFFSET);

			config_reg |= XQSPIPSS_CONFIG_MANSRT_MASK;
			xqspipss_write(xqspi->regs + XQSPIPSS_CONFIG_OFFSET,
				config_reg);
			spin_unlock(&xqspi->config_reg_lock);
		} else {
			/* If transfer and receive is completed then only send
			 * complete signal */
			if (!xqspi->bytes_to_receive)
				complete(&xqspi->done);
		}
	}

	return IRQ_HANDLED;
}

/**
 * xqspipss_start_transfer - Initiates the QSPI transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the QSPI transfer, and waits for the
 * transfer to be completed.
 *
 * returns:	Number of bytes transferred in the last transfer
 **/
static int xqspipss_start_transfer(struct spi_device *qspi,
			struct spi_transfer *transfer)
{
	struct xqspipss *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg;
	unsigned long flags;
	u32 data = 0;
	u8 instruction = 0;
	u8 index;

	xqspi->txbuf = transfer->tx_buf;
	xqspi->rxbuf = transfer->rx_buf;
	xqspi->bytes_to_transfer = transfer->len;
	xqspi->bytes_to_receive = transfer->len;

	if (xqspi->txbuf)
		instruction = *(u8 *)xqspi->txbuf;

	if (instruction) {
		for (index = 0 ; index < sizeof(flash_inst); index++)
			if (instruction == flash_inst[index].opcode)
				break;

		/* Instruction is not supported, return error */
		if (index == sizeof(flash_inst))
			return 0;

		xqspi->curr_inst = &flash_inst[index];
		xqspi->inst_response = 1;

		/* Get the instruction */
		data = 0;
		xqspipss_copy_write_data(xqspi, &data,
			xqspi->curr_inst->inst_size);

		/* Write the instruction to LSB of the FIFO. The core is
		 * designed such that it is not necessary to check whether the
		 * write FIFO is full before writing. However, write would be
		 * delayed if the user tries to write when write FIFO is full
		 */
		xqspipss_write(xqspi->regs + xqspi->curr_inst->offset, data);

		/* Read status register and Read ID instructions don't require
		 * to ignore the extra bytes in response of instruction as
		 * response contains the value */
		if ((instruction == XQSPIPSS_FLASH_OPCODE_RDSR1) ||
			(instruction == XQSPIPSS_FLASH_OPCODE_RDSR2) ||
			(instruction == XQSPIPSS_FLASH_OPCODE_RDID)) {
			if (xqspi->bytes_to_transfer < 4)
				xqspi->bytes_to_transfer = 0;
			else
				xqspi->bytes_to_transfer -= 3;
		}
	}

	INIT_COMPLETION(xqspi->done);
	if (xqspi->bytes_to_transfer)
		xqspipss_fill_tx_fifo(xqspi);
	xqspipss_write(xqspi->regs + XQSPIPSS_IEN_OFFSET,
			XQSPIPSS_IXR_ALL_MASK);
	/* Start the transfer by enabling manual start bit */
	spin_lock_irqsave(&xqspi->config_reg_lock, flags);
	config_reg = xqspipss_read(xqspi->regs +
			XQSPIPSS_CONFIG_OFFSET) | XQSPIPSS_CONFIG_MANSRT_MASK;
	xqspipss_write(xqspi->regs + XQSPIPSS_CONFIG_OFFSET, config_reg);
	spin_unlock_irqrestore(&xqspi->config_reg_lock, flags);

	wait_for_completion(&xqspi->done);

	return (transfer->len) - (xqspi->bytes_to_transfer);
}

/**
 * xqspipss_work_queue - Get the request from queue to perform transfers
 * @work:	Pointer to the work_struct structure
 **/
static void xqspipss_work_queue(struct work_struct *work)
{
	struct xqspipss *xqspi = container_of(work, struct xqspipss, work);
	unsigned long flags;

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	xqspi->dev_busy = 1;

	/* Check if list is empty or queue is stoped */
	if (list_empty(&xqspi->queue) ||
		xqspi->queue_state == XQSPIPSS_QUEUE_STOPPED) {
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

		list_for_each_entry(transfer, &msg->transfers, transfer_list) {
			if (transfer->bits_per_word || transfer->speed_hz) {
				status =
					xqspipss_setup_transfer(qspi, transfer);
				if (status < 0)
					break;
			}

			/* Select the chip if required */
			if (cs_change)
				xqspipss_chipselect(qspi, 1);

			cs_change = transfer->cs_change;

			if (!transfer->tx_buf && !transfer->rx_buf &&
				transfer->len) {
				status = -EINVAL;
				break;
			}

			/* Request the transfer */
			if (transfer->len)
				status =
					xqspipss_start_transfer(qspi, transfer);

			if (status != transfer->len) {
				if (status > 0)
					status = -EMSGSIZE;
				break;
			}
			msg->actual_length += status;
			status = 0;

			if (transfer->delay_usecs)
				udelay(transfer->delay_usecs);

			if (!cs_change)
				continue;
			if (transfer->transfer_list.next == &msg->transfers)
				break;

			/* Deselect the chip */
			xqspipss_chipselect(qspi, 0);
		}

		msg->status = status;
		msg->complete(msg->context);

		xqspipss_setup_transfer(qspi, NULL);

		if (!(status == 0 && cs_change))
			xqspipss_chipselect(qspi, 0);

		spin_lock_irqsave(&xqspi->trans_queue_lock, flags);
	}
	xqspi->dev_busy = 0;
	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
}

/**
 * xqspipss_transfer - Add a new transfer request at the tail of work queue
 * @qspi:	Pointer to the spi_device structure
 * @message:	Pointer to the spi_transfer structure which provides information
 *		about next transfer parameters
 *
 * returns:	0 on success, -EINVAL on invalid input parameter and
 *		-ESHUTDOWN if queue is stopped by module unload function
 **/
static int
xqspipss_transfer(struct spi_device *qspi, struct spi_message *message)
{
	struct xqspipss *xqspi = spi_master_get_devdata(qspi->master);
	struct spi_transfer *transfer;
	unsigned long flags;

	if (xqspi->queue_state == XQSPIPSS_QUEUE_STOPPED)
		return -ESHUTDOWN;

	message->actual_length = 0;
	message->status = -EINPROGRESS;

	/* Check each transfer's parameters */
	list_for_each_entry(transfer, &message->transfers, transfer_list) {
		u8 bits_per_word =
			transfer->bits_per_word ? : qspi->bits_per_word;

		bits_per_word = bits_per_word ? : 32;
		if (!transfer->tx_buf && !transfer->rx_buf && transfer->len)
			return -EINVAL;
		if (bits_per_word != 32)
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
 * xqspipss_start_queue - Starts the queue of the QSPI driver
 * @xqspi:	Pointer to the xqspipss structure
 *
 * returns:	0 on success and -EBUSY if queue is already running or device is
 *		busy
 **/
static inline int xqspipss_start_queue(struct xqspipss *xqspi)
{
	unsigned long flags;

	spin_lock_irqsave(&xqspi->trans_queue_lock, flags);

	if (xqspi->queue_state == XQSPIPSS_QUEUE_RUNNING || xqspi->dev_busy) {
		spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);
		return -EBUSY;
	}

	xqspi->queue_state = XQSPIPSS_QUEUE_RUNNING;
	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);

	return 0;
}

/**
 * xqspipss_stop_queue - Stops the queue of the QSPI driver
 * @xqspi:	Pointer to the xqspipss structure
 *
 * This function waits till queue is empty and then stops the queue.
 * Maximum time out is set to 5 seconds.
 *
 * returns:	0 on success and -EBUSY if queue is not empty or device is busy
 **/
static inline int xqspipss_stop_queue(struct xqspipss *xqspi)
{
	unsigned long flags;
	unsigned limit = 500;
	int ret = 0;

	if (xqspi->queue_state != XQSPIPSS_QUEUE_RUNNING)
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
		xqspi->queue_state = XQSPIPSS_QUEUE_STOPPED;

	spin_unlock_irqrestore(&xqspi->trans_queue_lock, flags);

	return ret;
}

/**
 * xqspipss_destroy_queue - Destroys the queue of the QSPI driver
 * @xqspi:	Pointer to the xqspipss structure
 *
 * returns:	0 on success and error value on failure
 **/
static inline int xqspipss_destroy_queue(struct xqspipss *xqspi)
{
	int ret;

	ret = xqspipss_stop_queue(xqspi);
	if (ret != 0)
		return ret;

	destroy_workqueue(xqspi->workqueue);

	return 0;
}

/**
 * xqspipss_probe - Probe method for the QSPI driver
 * @dev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * returns:	0 on success and error value on failure
 **/
static int __devinit xqspipss_probe(struct platform_device *dev)
{
	int ret = 0;
	struct spi_master *master;
	struct xqspipss *xqspi;
	struct resource *r;
	struct xspi_platform_data *platform_info;

	master = spi_alloc_master(&dev->dev, sizeof(struct xqspipss));
	if (master == NULL)
		return -ENOMEM;

	xqspi = spi_master_get_devdata(master);
	platform_set_drvdata(dev, master);

	platform_info = dev->dev.platform_data;
	if (platform_info == NULL) {
		ret = -ENODEV;
		dev_err(&dev->dev, "platform data not available\n");
		goto put_master;
	}

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENODEV;
		dev_err(&dev->dev, "platform_get_resource failed\n");
		goto put_master;
	}

	if (!request_mem_region(r->start,
			r->end - r->start + 1, dev->name)) {
		ret = -ENXIO;
		dev_err(&dev->dev, "request_mem_region failed\n");
		goto put_master;
	}

	xqspi->regs = ioremap(r->start, r->end - r->start + 1);
	if (xqspi->regs == NULL) {
		ret = -ENOMEM;
		dev_err(&dev->dev, "ioremap failed\n");
		goto release_mem;
	}

	xqspi->irq = platform_get_irq(dev, 0);
	if (xqspi->irq < 0) {
		ret = -ENXIO;
		dev_err(&dev->dev, "irq resource not found\n");
		goto unmap_io;
	}

	ret = request_irq(xqspi->irq, xqspipss_irq, 0, dev->name, xqspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&dev->dev, "request_irq failed\n");
		goto unmap_io;
	}

	/* QSPI controller initializations */
	xqspipss_init_hw(xqspi->regs);

	init_completion(&xqspi->done);
	master->bus_num = platform_info->bus_num;
	master->num_chipselect = platform_info->num_chipselect;
	master->setup = xqspipss_setup;
	master->transfer = xqspipss_transfer;
	xqspi->input_clk_hz = platform_info->speed_hz;
	xqspi->speed_hz = platform_info->speed_hz / 2;
	xqspi->dev_busy = 0;

	INIT_LIST_HEAD(&xqspi->queue);
	spin_lock_init(&xqspi->trans_queue_lock);
	spin_lock_init(&xqspi->config_reg_lock);

	xqspi->queue_state = XQSPIPSS_QUEUE_STOPPED;
	xqspi->dev_busy = 0;

	INIT_WORK(&xqspi->work, xqspipss_work_queue);
	xqspi->workqueue =
		create_singlethread_workqueue(dev_name(&master->dev));
	if (!xqspi->workqueue) {
		ret = -ENOMEM;
		dev_err(&dev->dev, "problem initializing queue\n");
		goto free_irq;
	}

	ret = xqspipss_start_queue(xqspi);
	if (ret != 0) {
		dev_err(&dev->dev, "problem starting queue\n");
		goto remove_queue;
	}

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&dev->dev, "spi_register_master failed\n");
		goto remove_queue;
	}

	dev_info(&dev->dev, "at 0x%08X mapped to 0x%08X, irq=%d\n", r->start,
		 (u32 __force)xqspi->regs, xqspi->irq);

	return ret;

remove_queue:
	(void)xqspipss_destroy_queue(xqspi);
free_irq:
	free_irq(xqspi->irq, xqspi);
unmap_io:
	iounmap(xqspi->regs);
release_mem:
	release_mem_region(r->start, r->end - r->start + 1);
put_master:
	platform_set_drvdata(dev, NULL);
	spi_master_put(master);
	return ret;
}

/**
 * xqspipss_remove - Remove method for the QSPI driver
 * @dev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * returns:	0 on success and error value on failure
 **/
static int __devexit xqspipss_remove(struct platform_device *dev)
{
	struct spi_master *master = platform_get_drvdata(dev);
	struct xqspipss *xqspi = spi_master_get_devdata(master);
	struct resource *r;
	int ret = 0;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&dev->dev, "platform_get_resource failed\n");
		return -ENODEV;
	}

	ret = xqspipss_destroy_queue(xqspi);
	if (ret != 0)
		return ret;

	xqspipss_write(xqspi->regs + XQSPIPSS_ENABLE_OFFSET,
			~XQSPIPSS_ENABLE_ENABLE_MASK);

	free_irq(xqspi->irq, xqspi);
	iounmap(xqspi->regs);
	release_mem_region(r->start, r->end - r->start + 1);

	spi_unregister_master(master);
	spi_master_put(master);

	/* Prevent double remove */
	platform_set_drvdata(dev, NULL);

	dev_dbg(&dev->dev, "remove succeeded\n");
	return 0;
}

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:" DRIVER_NAME);

/*
 * xqspipss_driver - This structure defines the QSPI platform driver
 */
static struct platform_driver xqspipss_driver = {
	.probe	= xqspipss_probe,
	.remove	= __devexit_p(xqspipss_remove),
	.suspend = NULL,
	.resume = NULL,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

/**
 * xqspipss_init - QSPI driver module initialization function
 *
 * returns:	0 on success and error value on failure
 **/
static int __init xqspipss_init(void)
{
	return platform_driver_register(&xqspipss_driver);
}

module_init(xqspipss_init);

/**
 * xqspipss_exit - QSPI driver module exit function
 **/
static void __exit xqspipss_exit(void)
{
	platform_driver_unregister(&xqspipss_driver);
}

module_exit(xqspipss_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx PSS QSPI driver");
MODULE_LICENSE("GPL");

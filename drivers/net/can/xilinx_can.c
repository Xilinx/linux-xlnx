/* Xilinx CAN device driver
 *
 * Copyright (C) 2012 - 2014 Xilinx, Inc.
 * Copyright (C) 2009 PetaLogix. All rights reserved.
 *
 * Description:
 * This driver is developed for Axi CAN IP and for Zynq CANPS Controller.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>

#define DRIVER_NAME	"xilinx_can"

/* CAN registers set */
enum xcan_reg {
	XCAN_SRR_OFFSET		= 0x00, /* Software reset */
	XCAN_MSR_OFFSET		= 0x04, /* Mode select */
	XCAN_BRPR_OFFSET	= 0x08, /* Baud rate prescaler */
	XCAN_BTR_OFFSET		= 0x0C, /* Bit timing */
	XCAN_ECR_OFFSET		= 0x10, /* Error counter */
	XCAN_ESR_OFFSET		= 0x14, /* Error status */
	XCAN_SR_OFFSET		= 0x18, /* Status */
	XCAN_ISR_OFFSET		= 0x1C, /* Interrupt status */
	XCAN_IER_OFFSET		= 0x20, /* Interrupt enable */
	XCAN_ICR_OFFSET		= 0x24, /* Interrupt clear */
	XCAN_TXFIFO_ID_OFFSET	= 0x30,/* TX FIFO ID */
	XCAN_TXFIFO_DLC_OFFSET	= 0x34, /* TX FIFO DLC */
	XCAN_TXFIFO_DW1_OFFSET	= 0x38, /* TX FIFO Data Word 1 */
	XCAN_TXFIFO_DW2_OFFSET	= 0x3C, /* TX FIFO Data Word 2 */
	XCAN_RXFIFO_ID_OFFSET	= 0x50, /* RX FIFO ID */
	XCAN_RXFIFO_DLC_OFFSET	= 0x54, /* RX FIFO DLC */
	XCAN_RXFIFO_DW1_OFFSET	= 0x58, /* RX FIFO Data Word 1 */
	XCAN_RXFIFO_DW2_OFFSET	= 0x5C, /* RX FIFO Data Word 2 */
	XCAN_F_BRPR_OFFSET	= 0x088, /* Data Phase Buad Rate
					  * Prescalar
					  */
	XCAN_F_BTR_OFFSET	= 0x08C, /* Data Phase Bit Timing */
	XCAN_TRR_OFFSET		= 0x090, /* Tx Buffer Ready Request */
	XCAN_IETRS_OFFSET	= 0x094, /* TRR Served Interrupt
					  * Enable
					  */
	XCANFD_TXFIFO_ID_OFFSET	= 0x0100, /* Tx Message Buffer Element
					   * ID
					   */
	XCANFD_TXFIFO_DLC_OFFSET = 0x0104, /* Tx Message Buffer Element
					    * DLC
					    */
	XCANFD_TXFIFO_DW_OFFSET	= 0x0108, /* Tx Message Buffer Element
					   * DW
					   */
	XCANFD_RXFIFO_ID_OFFSET	= 0x1100, /* Rx Message Buffer Element
					   * ID
					   */
	XCANFD_RXFIFO_DLC_OFFSET = 0x1104, /* Rx Message Buffer Element
					    * DLC
					    */
	XCANFD_RXFIFO_DW_OFFSET	= 0x1108, /* Rx Message Buffer Element
					   * DW
					   */
	XCAN_AFMR_BASE_OFFSET	= 0x1A00, /* Acceptance Filter */
	XCAN_AFIDR_BASE_OFFSET	= 0x1A04, /* Acceptance Filter ID */
	XCAN_AFR_OFFSET		= 0x0E0, /* Acceptance Filter */
	XCAN_FSR_OFFSET		= 0x0E8, /* Receive FIFO Status */
	XCAN_TIMESTAMPR_OFFSET	= 0x0028, /* Time Stamp */
};

/* CAN register bit masks - XCAN_<REG>_<BIT>_MASK */
#define XCAN_SRR_CEN_MASK		0x00000002 /* CAN enable */
#define XCAN_SRR_RESET_MASK		0x00000001 /* Soft Reset the CAN core */
#define XCAN_MSR_LBACK_MASK		0x00000002 /* Loop back mode select */
#define XCAN_MSR_SLEEP_MASK		0x00000001 /* Sleep mode select */
#define XCAN_BRPR_BRP_MASK		0x000000FF /* Baud rate prescaler */
#define XCAN_BTR_SJW_MASK		0x00000180 /* Synchronous jump width */
#define XCAN_BTR_TS2_MASK		0x00000070 /* Time segment 2 */
#define XCAN_BTR_TS1_MASK		0x0000000F /* Time segment 1 */
#define XCANFD_BTR_SJW_MASK		0x000F0000 /* Sync Jump Width */
#define XCANFD_BTR_TS2_MASK		0x00000F00 /* Time Segment 2 */
#define XCANFD_BTR_TS1_MASK		0x0000003F /* Time Segment 1 */
#define XCAN_ECR_REC_MASK		0x0000FF00 /* Receive error counter */
#define XCAN_ECR_TEC_MASK		0x000000FF /* Transmit error counter */
#define XCAN_ESR_ACKER_MASK		0x00000010 /* ACK error */
#define XCAN_ESR_BERR_MASK		0x00000008 /* Bit error */
#define XCAN_ESR_STER_MASK		0x00000004 /* Stuff error */
#define XCAN_ESR_FMER_MASK		0x00000002 /* Form error */
#define XCAN_ESR_CRCER_MASK		0x00000001 /* CRC error */
#define XCAN_SR_TXFLL_MASK		0x00000400 /* TX FIFO is full */
#define XCAN_SR_ESTAT_MASK		0x00000180 /* Error status */
#define XCAN_SR_ERRWRN_MASK		0x00000040 /* Error warning */
#define XCAN_SR_NORMAL_MASK		0x00000008 /* Normal mode */
#define XCAN_SR_LBACK_MASK		0x00000002 /* Loop back mode */
#define XCAN_SR_CONFIG_MASK		0x00000001 /* Configuration mode */
#define XCAN_IXR_TXFEMP_MASK		0x00004000 /* TX FIFO Empty */
#define XCAN_IXR_WKUP_MASK		0x00000800 /* Wake up interrupt */
#define XCAN_IXR_SLP_MASK		0x00000400 /* Sleep interrupt */
#define XCAN_IXR_BSOFF_MASK		0x00000200 /* Bus off interrupt */
#define XCAN_IXR_ERROR_MASK		0x00000100 /* Error interrupt */
#define XCAN_IXR_RXNEMP_MASK		0x00000080 /* RX FIFO NotEmpty intr */
#define XCAN_IXR_RXOFLW_MASK		0x00000040 /* RX FIFO Overflow intr */
#define XCAN_IXR_RXOK_MASK		0x00000010 /* Message received intr */
#define XCAN_IXR_TXFLL_MASK		0x00000004 /* Tx FIFO Full intr */
#define XCAN_IXR_TXOK_MASK		0x00000002 /* TX successful intr */
#define XCAN_IXR_ARBLST_MASK		0x00000001 /* Arbitration lost intr */
#define XCAN_IDR_ID1_MASK		0xFFE00000 /* Standard msg identifier */
#define XCAN_IDR_SRR_MASK		0x00100000 /* Substitute remote TXreq */
#define XCAN_IDR_IDE_MASK		0x00080000 /* Identifier extension */
#define XCAN_IDR_ID2_MASK		0x0007FFFE /* Extended message ident */
#define XCAN_IDR_RTR_MASK		0x00000001 /* Remote TX request */
#define XCAN_DLCR_DLC_MASK		0xF0000000 /* Data length code */
#define XCAN_MSR_BRSD_MASK		0x00000008 /* Bit Rate Switch Select */
#define XCAN_MSR_SNOOP_MASK		0x00000004 /* Snoop Mode Select */
#define XCAN_MSR_DPEE_MASK		0x00000020 /* Protocol Exception
						    * Event
						    */
#define XCAN_MSR_SBR_MASK		0x00000040 /* Start Bus-Off Recovery */
#define XCAN_MSR_ABR_MASK		0x00000080 /* Auto Bus-Off Recovery */
#define XCAN_MSR_CONFIG_MASK		0x000000F8 /* Configuration Mode */
#define XCAN_F_BRPR_TDCMASK		0x00001F00 /* TDC Value */
#define XCAN_F_BTR_SJW_MASK		0x00070000 /* Sync Jump Width */
#define XCAN_F_BTR_TS2_MASK		0x00000700 /* Time Segment 2 */
#define XCAN_F_BTR_TS1_MASK		0x0000000F /* Time Segment 1 */
#define XCAN_ESR_F_BERR_MASK		0x00000800 /* F_Bit Error */
#define XCAN_ESR_F_STER_MASK		0x00000400 /* F_Stuff Error */
#define XCAN_ESR_F_FMER_MASK		0x00000200 /* F_Form Error */
#define XCAN_ESR_F_CRCER_MASK		0x00000100 /* F_CRC Error */
#define XCAN_SR_SNOOP_MASK		0x00001000 /* Snoop Mode */
#define XCAN_SR_BBSY_MASK		0x00000020 /* Bus Busy */
#define XCAN_SR_BIDLE_MASK		0x00000010 /* Bus Idle */
#define XCAN_SR_SLEEP_MASK		0x00000004 /* Sleep Mode */
#define XCAN_SR_PEE_CONFIG_MASK		0x00000200 /* Protocol Exception
						    * Mode Indicator
						    */
#define XCAN_SR_BSFR_CONFIG_MASK	0x00000400 /* Bus-Off recovery
						    * Mode Indicator
						    */
#define XCAN_SR_NISO_MASK	0x00000800 /* Non-ISO Core */
#define XCAN_FSR_FL_MASK	0x00003F00 /* Fill Level */
#define XCAN_FSR_RI_MASK	0x0000001F /* Read Index */
#define XCAN_FSR_IRI_MASK	0x00000080 /* Increment Read Index */
#define XCAN_IXR_RXMNF_MASK	0x00020000 /* Rx Match Not Finished Intr */
#define XCAN_IXR_TXRRS_MASK	0x00002000 /* Tx Buffer Ready Request Served
					    * Intr
					    */
#define XCAN_IXR_PEE_MASK	0x00000004 /* Protocol Exception Intr */
#define XCAN_IXR_BSRD_MASK	0x00000008 /* Bus-Off recovery done Intr */
#define XCAN_AFR_ENABLE_ALL	0xFFFFFFFF /* All filter Enable */
#define XCAN_DLCR_EDL_MASK	0x08000000 /* EDL Mask in DLC */
#define XCAN_DLCR_BRS_MASK	0x04000000 /* BRS Mask in DLC */
#define XCAN_DLCR_DLC_SHIFT	28 /* BRS Mask in DLC */
#define XCAN_DLCR_EDL_SHIFT	27 /* EDL Mask in DLC */
#define XCAN_DLCR_BRS_SHIFT	26

#define XCAN_INTR_ALL	(XCAN_IXR_TXOK_MASK | XCAN_IXR_BSOFF_MASK |\
			 XCAN_IXR_WKUP_MASK | XCAN_IXR_SLP_MASK | \
			 XCAN_IXR_ERROR_MASK | XCAN_IXR_RXOFLW_MASK | \
			 XCAN_IXR_ARBLST_MASK)

/* CAN register bit shift - XCAN_<REG>_<BIT>_SHIFT */
#define XCAN_BTR_SJW_SHIFT	7 /* Synchronous jump width */
#define XCAN_BTR_TS2_SHIFT	4 /* Time segment 2 */
#define XCANFD_BTR_SJW_SHIFT	16 /* Sync Jump Width Shift */
#define XCANFD_BTR_TS2_SHIFT	8 /* Time Segment 2 Shift */
#define XCAN_SR_ESTAT_SHIFT	7 /* Error Status Shift */
#define XCAN_RXLRM_BI_SHIFT	18 /* Rx Buffer Index Shift Value */
#define XCAN_CSB_SHIFT		16 /* Core Status Bit Shift Value */
#define XCAN_IDR_SRR_SHIFT	20 /* Soft Reset Shift */
#define XCAN_IDR_IDE_SHIFT	19 /* Identifier Extension Shift */
#define XCAN_IDR_ID1_SHIFT	21 /* Standard Messg Identifier */
#define XCAN_IDR_ID2_SHIFT	1 /* Extended Message Identifier */
#define XCAN_DLCR_DLC_SHIFT	28 /* Data length code */
#define XCAN_ESR_REC_SHIFT	8 /* Rx Error Count */

/* CAN frame length constants */
#define XCAN_FRAME_MAX_DATA_LEN		8
#define XCAN_TIMEOUT			(1 * HZ)
#define XCANFD_MAX_FRAME_LEN		72
#define XCANFD_FRAME_MAX_DATA_LEN	64
#define XCANFD_DW_BYTES			4
#define XCANFD_CTRLREG_WIDTH		4

/* Quirks */
#define CANFD_SUPPORT	BIT(0)

/* CANFD Tx and Rx Ram offsets */
#define XCANFD_TXDW_OFFSET(n)		(XCANFD_TXFIFO_DW_OFFSET + ((n) * \
					XCANFD_MAX_FRAME_LEN))
#define XCANFD_TXID_OFFSET(n)		(XCANFD_TXFIFO_ID_OFFSET + ((n) * \
					XCANFD_MAX_FRAME_LEN))
#define XCANFD_TXDLC_OFFSET(n)		(XCANFD_TXFIFO_DLC_OFFSET + ((n) *\
					XCANFD_MAX_FRAME_LEN))
#define XCANFD_RXDLC_OFFSET(readindex)	(XCANFD_RXFIFO_DLC_OFFSET + \
					((readindex) * XCANFD_MAX_FRAME_LEN))
#define XCANFD_RXID_OFFSET(readindex)	(XCANFD_RXFIFO_ID_OFFSET + \
					((readindex) * XCANFD_MAX_FRAME_LEN))
#define XCANFD_RXDW_OFFSET(readindex)	(XCANFD_RXFIFO_DW_OFFSET + \
					((readindex) * XCANFD_MAX_FRAME_LEN))

/**
 * struct xcan_priv - This definition define CAN driver instance
 * @can:			CAN private data structure.
 * @tx_head:			Tx CAN packets ready to send on the queue
 * @tx_tail:			Tx CAN packets successfully sended on the queue
 * @tx_max:			Maximum number packets the driver can send
 * @napi:			NAPI structure
 * @read_reg:			For reading data from CAN registers
 * @write_reg:			For writing data to CAN registers
 * @dev:			Network device data structure
 * @reg_base:			Ioremapped address to registers
 * @irq_flags:			For request_irq()
 * @bus_clk:			Pointer to struct clk
 * @can_clk:			Pointer to struct clk
 * @quirks:			Needed for different IP cores
 */
struct xcan_priv {
	struct can_priv can;
	unsigned int tx_head;
	unsigned int tx_tail;
	unsigned int tx_max;
	struct napi_struct napi;
	u32 (*read_reg)(const struct xcan_priv *priv, enum xcan_reg reg);
	void (*write_reg)(const struct xcan_priv *priv, enum xcan_reg reg,
			  u32 val);
	struct device *dev;
	void __iomem *reg_base;
	unsigned long irq_flags;
	struct clk *bus_clk;
	struct clk *can_clk;
	u32 quirks;
};

struct xcan_platform_data {
	u32 quirks;
};

/* CAN Bittiming constants as per Xilinx CAN specs */
static struct can_bittiming_const xcan_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

/* CAN Data Bittiming constants as per Xilinx CAN specs */
static struct can_bittiming_const xcan_data_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

/**
 * xcan_write_reg_le - Write a value to the device register little endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 * @val:	Value to write at the Register offset
 *
 * Write data to the paricular CAN register
 */
static void xcan_write_reg_le(const struct xcan_priv *priv, enum xcan_reg reg,
			      u32 val)
{
	iowrite32(val, priv->reg_base + reg);
}

/**
 * xcan_read_reg_le - Read a value from the device register little endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 *
 * Read data from the particular CAN register
 * Return: value read from the CAN register
 */
static u32 xcan_read_reg_le(const struct xcan_priv *priv, enum xcan_reg reg)
{
	return ioread32(priv->reg_base + reg);
}

/**
 * xcan_write_reg_be - Write a value to the device register big endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 * @val:	Value to write at the Register offset
 *
 * Write data to the paricular CAN register
 */
static void xcan_write_reg_be(const struct xcan_priv *priv, enum xcan_reg reg,
			      u32 val)
{
	iowrite32be(val, priv->reg_base + reg);
}

/**
 * xcan_read_reg_be - Read a value from the device register big endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 *
 * Read data from the particular CAN register
 * Return: value read from the CAN register
 */
static u32 xcan_read_reg_be(const struct xcan_priv *priv, enum xcan_reg reg)
{
	return ioread32be(priv->reg_base + reg);
}

/**
 * set_reset_mode - Resets the CAN device mode
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver reset mode routine.The driver
 * enters into configuration mode.
 *
 * Return: 0 on success and failure value on error
 */
static int set_reset_mode(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	unsigned long timeout;

	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);

	timeout = jiffies + XCAN_TIMEOUT;
	while (!(priv->read_reg(priv, XCAN_SR_OFFSET) & XCAN_SR_CONFIG_MASK)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev, "timed out for config mode\n");
			return -ETIMEDOUT;
		}
		usleep_range(500, 10000);
	}

	return 0;
}

/**
 * xcan_set_bittiming - CAN set bit timing routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver set bittiming  routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_set_bittiming(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	u32 btr0, btr1;
	u32 is_config_mode;

	/* Check whether Xilinx CAN is in configuration mode.
	 * It cannot set bit timing if Xilinx CAN is not in configuration mode.
	 */
	is_config_mode = priv->read_reg(priv, XCAN_SR_OFFSET) &
				XCAN_SR_CONFIG_MASK;
	if (!is_config_mode) {
		netdev_alert(ndev,
			     "BUG! Cannot set bittiming - CAN is not in config mode\n");
		return -EPERM;
	}

	/* Setting Baud Rate prescalar value in BRPR Register */
	btr0 = (bt->brp - 1);

	/* Setting Time Segment 1 in BTR Register */
	btr1 = (bt->prop_seg + bt->phase_seg1 - 1);

	/* Setting Time Segment 2 in BTR Register */
	btr1 |= (bt->phase_seg2 - 1) << ((priv->quirks & CANFD_SUPPORT) ?
			XCANFD_BTR_TS2_SHIFT : XCAN_BTR_TS2_SHIFT);

	/* Setting Synchronous jump width in BTR Register */
	btr1 |= (bt->sjw - 1) << ((priv->quirks & CANFD_SUPPORT) ?
			XCANFD_BTR_SJW_SHIFT : XCAN_BTR_SJW_SHIFT);

	priv->write_reg(priv, XCAN_BRPR_OFFSET, btr0);
	priv->write_reg(priv, XCAN_BTR_OFFSET, btr1);

	netdev_dbg(ndev, "BRPR=0x%08x, BTR=0x%08x\n",
		   priv->read_reg(priv, XCAN_BRPR_OFFSET),
		   priv->read_reg(priv, XCAN_BTR_OFFSET));

	if (priv->quirks & CANFD_SUPPORT) {
		/* Setting Baud Rate prescalar value in F_BRPR Register */
		btr0 = dbt->brp - 1;

		/* Setting Time Segment 1 in BTR Register */
		btr1 = dbt->prop_seg + bt->phase_seg1 - 1;

		/* Setting Time Segment 2 in BTR Register */
		btr1 |= (dbt->phase_seg2 - 1) << XCAN_BTR_TS2_SHIFT;

		/* Setting Synchronous jump width in BTR Register */
		btr1 |= (dbt->sjw - 1) << XCAN_BTR_SJW_SHIFT;

		priv->write_reg(priv, XCAN_F_BRPR_OFFSET, btr0);
		priv->write_reg(priv, XCAN_F_BTR_OFFSET, btr1);
	}
	netdev_dbg(ndev, "F_BRPR=0x%08x, F_BTR=0x%08x\n",
		   priv->read_reg(priv, XCAN_F_BRPR_OFFSET),
		   priv->read_reg(priv, XCAN_F_BTR_OFFSET));

	return 0;
}

/**
 * xcan_chip_start - This the drivers start routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the drivers start routine.
 * Based on the State of the CAN device it puts
 * the CAN device into a proper mode.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_chip_start(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 reg_msr, reg_sr_mask, intr_all = 0;
	int err;
	unsigned long timeout;

	/* Check if it is in reset mode */
	err = set_reset_mode(ndev);
	if (err < 0)
		return err;

	err = xcan_set_bittiming(ndev);
	if (err < 0)
		return err;

	/* Enable interrupts */
	if (priv->quirks & CANFD_SUPPORT) {
		intr_all = XCAN_INTR_ALL | XCAN_IXR_PEE_MASK |
				XCAN_IXR_BSRD_MASK | XCAN_IXR_RXMNF_MASK |
				XCAN_IXR_TXRRS_MASK | XCAN_IXR_RXOK_MASK;
	} else {
		intr_all = XCAN_INTR_ALL | XCAN_IXR_RXNEMP_MASK;
	}

	priv->write_reg(priv, XCAN_IER_OFFSET, intr_all);

	/* Check whether it is loopback mode or normal mode  */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		reg_msr = XCAN_MSR_LBACK_MASK;
		reg_sr_mask = XCAN_SR_LBACK_MASK;
	} else {
		reg_msr = 0x0;
		reg_sr_mask = XCAN_SR_NORMAL_MASK;
	}

	if (priv->quirks & CANFD_SUPPORT) {
		/* As per Xilinx canfd spec, default filter enabling is
		 * required
		 */
		priv->write_reg(priv, XCAN_AFR_OFFSET, XCAN_AFR_ENABLE_ALL);
	}
	priv->write_reg(priv, XCAN_MSR_OFFSET, reg_msr);
	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_CEN_MASK);

	timeout = jiffies + XCAN_TIMEOUT;
	while (!(priv->read_reg(priv, XCAN_SR_OFFSET) & reg_sr_mask)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev,
				    "timed out for correct mode\n");
			return -ETIMEDOUT;
		}
	}
	netdev_dbg(ndev, "status:#x%08x\n",
		   priv->read_reg(priv, XCAN_SR_OFFSET));

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	priv->tx_head = 0;
	priv->tx_tail = 0;

	return 0;
}

/**
 * xcan_do_set_mode - This sets the mode of the driver
 * @ndev:	Pointer to net_device structure
 * @mode:	Tells the mode of the driver
 *
 * This check the drivers state and calls the
 * the corresponding modes to set.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		ret = xcan_chip_start(ndev);
		if (ret < 0) {
			netdev_err(ndev, "xcan_chip_start failed!\n");
			return ret;
		}
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/**
 * xcan_get_freebuffer - Checks free buffer in the configured buffers
 * @priv:	Driver private data structure
 *
 * While sending data, need to find free buffer from the tx
 * buffers avialable and then write data to that buffer.
 *
 * Return: Free Buffer on success and -1 if no buffer available
 */
static int xcan_get_freebuffer(struct xcan_priv *priv)
{
	u32 bufindex = 0, trrregval = 0;

	trrregval = priv->read_reg(priv, XCAN_TRR_OFFSET);
	for (bufindex = 0; bufindex < priv->tx_max; bufindex++) {
		if (trrregval & (1 << bufindex))
			continue;
		return bufindex;
	}
	return -1;
}

/**
 * xcan_start_xmit - Starts the transmission
 * @skb:	sk_buff pointer that contains data to be Txed
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from upper layers to initiate transmission. This
 * function uses the next available free txbuff and populates their fields to
 * start the transmission.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 id, dlc, data[2] = {0, 0};
	u32 buffnr, ramoff, dwindex = 0, i, trrval;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (!(priv->quirks & CANFD_SUPPORT)) {
		/* Check if the TX buffer is full */
		if (unlikely(priv->read_reg(priv, XCAN_SR_OFFSET) &
				XCAN_SR_TXFLL_MASK)) {
			netif_stop_queue(ndev);
			netdev_err(ndev, "BUG!, TX FIFO full when queue awake!\n");
			return NETDEV_TX_BUSY;
		}
	}

	/* Watch carefully on the bit sequence */
	if (cf->can_id & CAN_EFF_FLAG) {
		/* Extended CAN ID format */
		id = ((cf->can_id & CAN_EFF_MASK) << XCAN_IDR_ID2_SHIFT) &
			XCAN_IDR_ID2_MASK;
		id |= (((cf->can_id & CAN_EFF_MASK) >>
			(CAN_EFF_ID_BITS - CAN_SFF_ID_BITS)) <<
			XCAN_IDR_ID1_SHIFT) & XCAN_IDR_ID1_MASK;

		/* The substibute remote TX request bit should be "1"
		 * for extended frames as in the Xilinx CAN datasheet
		 */
		id |= XCAN_IDR_IDE_MASK | XCAN_IDR_SRR_MASK;

		if (cf->can_id & CAN_RTR_FLAG)
			/* Extended frames remote TX request */
			id |= XCAN_IDR_RTR_MASK;
	} else {
		/* Standard CAN ID format */
		id = ((cf->can_id & CAN_SFF_MASK) << XCAN_IDR_ID1_SHIFT) &
			XCAN_IDR_ID1_MASK;

		if (cf->can_id & CAN_RTR_FLAG)
			/* Standard frames remote TX request */
			id |= XCAN_IDR_SRR_MASK;
	}

	dlc = can_len2dlc(cf->len) << XCAN_DLCR_DLC_SHIFT;
	if (priv->quirks & CANFD_SUPPORT) {
		if (can_is_canfd_skb(skb)) {
			if (cf->flags & CANFD_BRS)
				dlc |= XCAN_DLCR_BRS_MASK;
			dlc |= XCAN_DLCR_EDL_MASK;
		}

		can_put_echo_skb(skb, ndev, priv->tx_head % priv->tx_max);
		priv->tx_head++;
		buffnr = xcan_get_freebuffer(priv);
		if (buffnr == -1)
			netif_stop_queue(ndev);

		priv->write_reg(priv, XCANFD_TXID_OFFSET(buffnr), id);
		priv->write_reg(priv, XCANFD_TXDLC_OFFSET(buffnr), dlc);

		for (i = 0; i < cf->len; i += 4) {
			ramoff = XCANFD_TXDW_OFFSET(buffnr) + (dwindex *
					XCANFD_DW_BYTES);
			priv->write_reg(priv, ramoff,
					be32_to_cpup((__be32 *)(cf->data + i)));
			dwindex++;
		}

		trrval = priv->read_reg(priv, XCAN_TRR_OFFSET);
		trrval |= 1 << buffnr;
		priv->write_reg(priv, XCAN_TRR_OFFSET, trrval);
		stats->tx_bytes += cf->len;
		if (buffnr == -1)
			netif_stop_queue(ndev);
	} else {
		if (cf->len > 0)
			data[0] = be32_to_cpup((__be32 *)(cf->data + 0));
		if (cf->len > 4)
			data[1] = be32_to_cpup((__be32 *)(cf->data + 4));

		can_put_echo_skb(skb, ndev, priv->tx_head % priv->tx_max);
		priv->tx_head++;

		/* Write the Frame to Xilinx CAN TX FIFO */
		priv->write_reg(priv, XCAN_TXFIFO_ID_OFFSET, id);
		/* If the CAN frame is RTR frame this write triggers
		 * tranmission
		 */
		priv->write_reg(priv, XCAN_TXFIFO_DLC_OFFSET, dlc);
		if (!(cf->can_id & CAN_RTR_FLAG)) {
			priv->write_reg(priv, XCAN_TXFIFO_DW1_OFFSET, data[0]);
			/* If the CAN frame is Standard/Extended frame this
			 * write triggers tranmission
			 */
			priv->write_reg(priv, XCAN_TXFIFO_DW2_OFFSET, data[1]);
			stats->tx_bytes += cf->len;
		}
	}
	/* Check if the TX buffer is full */
	if ((priv->tx_head - priv->tx_tail) == priv->tx_max)
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
}

/**
 * xcan_rx -  Is called from CAN isr to complete the received
 *		frame  processing
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from the CAN isr(poll) to process the Rx frames. It
 * does minimal processing and invokes "netif_receive_skb" to complete further
 * processing.
 * Return: 1 on success and 0 on failure.
 */
static int xcan_rx(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 id_xcan, dlc, data[2] = {0, 0};

	/* Read a frame from Xilinx zynq CANPS */
	id_xcan = priv->read_reg(priv, XCAN_RXFIFO_ID_OFFSET);
	dlc = priv->read_reg(priv, XCAN_RXFIFO_DLC_OFFSET) >>
				XCAN_DLCR_DLC_SHIFT;
	skb = alloc_can_skb(ndev, &cf);
	if (unlikely(!skb)) {
		stats->rx_dropped++;
		return 0;
	}

	/* Change Xilinx CAN data length format to socketCAN data format */
	cf->can_dlc = get_can_dlc(dlc);

	/* Change Xilinx CAN ID format to socketCAN ID format */
	if (id_xcan & XCAN_IDR_IDE_MASK) {
		/* The received frame is an Extended format frame */
		cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >> 3;
		cf->can_id |= (id_xcan & XCAN_IDR_ID2_MASK) >>
				XCAN_IDR_ID2_SHIFT;
		cf->can_id |= CAN_EFF_FLAG;
		if (id_xcan & XCAN_IDR_RTR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	} else {
		/* The received frame is a standard format frame */
		cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >>
				XCAN_IDR_ID1_SHIFT;
		if (id_xcan & XCAN_IDR_SRR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	}

	/* DW1/DW2 must always be read to remove message from RXFIFO */
	data[0] = priv->read_reg(priv, XCAN_RXFIFO_DW1_OFFSET);
	data[1] = priv->read_reg(priv, XCAN_RXFIFO_DW2_OFFSET);

	if (!(cf->can_id & CAN_RTR_FLAG)) {
		/* Change Xilinx CAN data format to socketCAN data format */
		if (cf->can_dlc > 0)
			*(__be32 *)(cf->data) = cpu_to_be32(data[0]);
		if (cf->can_dlc > 4)
			*(__be32 *)(cf->data + 4) = cpu_to_be32(data[1]);
	}

	stats->rx_bytes += cf->can_dlc;
	stats->rx_packets++;
	netif_receive_skb(skb);

	return 1;
}

/**
 * xcanfd_rx -  Is called from CAN isr to complete the received
 *		frame  processing
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from the CAN isr(poll) to process the Rx frames. It
 * does minimal processing and invokes "netif_receive_skb" to complete further
 * processing.
 * Return: 1 on success and 0 on failure.
 */
static int xcanfd_rx(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 id_xcan, dlc, data[2] = {0, 0}, dwindex = 0, i, fsr, readindex;

	fsr = priv->read_reg(priv, XCAN_FSR_OFFSET);
	if (fsr & XCAN_FSR_FL_MASK) {
		readindex = fsr & XCAN_FSR_RI_MASK;
		id_xcan = priv->read_reg(priv, XCANFD_RXID_OFFSET(readindex));
		dlc = priv->read_reg(priv, XCANFD_RXDLC_OFFSET(readindex));
		if (dlc & XCAN_DLCR_EDL_MASK)
			skb = alloc_canfd_skb(ndev, &cf);
		else
			skb = alloc_can_skb(ndev, (struct can_frame **)&cf);

		if (unlikely(!skb)) {
			stats->rx_dropped++;
			return 0;
		}

		/* Change Xilinx CANFD data length format to socketCAN data
		 * format
		 */
		if (dlc & XCAN_DLCR_EDL_MASK)
			cf->len = can_dlc2len((dlc & XCAN_DLCR_DLC_MASK) >>
					  XCAN_DLCR_DLC_SHIFT);
		else
			cf->len = get_can_dlc((dlc & XCAN_DLCR_DLC_MASK) >>
						  XCAN_DLCR_DLC_SHIFT);

		/* Change Xilinx CAN ID format to socketCAN ID format */
		if (id_xcan & XCAN_IDR_IDE_MASK) {
			/* The received frame is an Extended format frame */
			cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >> 3;
			cf->can_id |= (id_xcan & XCAN_IDR_ID2_MASK) >>
					XCAN_IDR_ID2_SHIFT;
			cf->can_id |= CAN_EFF_FLAG;
			if (id_xcan & XCAN_IDR_RTR_MASK)
				cf->can_id |= CAN_RTR_FLAG;
		} else {
			/* The received frame is a standard format frame */
			cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >>
					XCAN_IDR_ID1_SHIFT;
			if (!(dlc & XCAN_DLCR_EDL_MASK) && (id_xcan &
						XCAN_IDR_SRR_MASK))
				cf->can_id |= CAN_RTR_FLAG;
		}

		/* Check the frame received is FD or not*/
		if (dlc & XCAN_DLCR_EDL_MASK) {
			for (i = 0; i < cf->len; i += 4) {
				data[0] = priv->read_reg(priv,
						(XCANFD_RXDW_OFFSET(readindex) +
						(dwindex * XCANFD_DW_BYTES)));
				*(__be32 *)(cf->data + i) = cpu_to_be32(
								data[0]);
				dwindex++;
			}
		} else {
			for (i = 0; i < cf->len; i += 4) {
				data[0] = priv->read_reg(priv,
					XCANFD_RXDW_OFFSET(readindex) + i);
				*(__be32 *)(cf->data + i) = cpu_to_be32(
							data[0]);
			}
		}
		/* Update FSR Register so that next packet will save to
		 * buffer
		 */
		fsr = priv->read_reg(priv, XCAN_FSR_OFFSET);
		fsr |= XCAN_FSR_IRI_MASK;
		priv->write_reg(priv, XCAN_FSR_OFFSET, fsr);
		fsr = priv->read_reg(priv, XCAN_FSR_OFFSET);
		stats->rx_bytes += cf->len;
		stats->rx_packets++;
		netif_receive_skb(skb);

		return 1;
	}
	/* If FSR Register is not updated with fill level */
	return 0;
}

static void xcan_chip_stop(struct net_device *ndev);
/**
 * xcan_err_interrupt - error frame Isr
 * @ndev:	net_device pointer
 * @isr:	interrupt status register value
 *
 * This is the CAN error interrupt and it will
 * check the the type of error and forward the error
 * frame to upper layers.
 */
static void xcan_err_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 err_status, status, txerr = 0, rxerr = 0;

	skb = alloc_can_err_skb(ndev, &cf);

	err_status = priv->read_reg(priv, XCAN_ESR_OFFSET);
	priv->write_reg(priv, XCAN_ESR_OFFSET, err_status);
	txerr = priv->read_reg(priv, XCAN_ECR_OFFSET) & XCAN_ECR_TEC_MASK;
	rxerr = ((priv->read_reg(priv, XCAN_ECR_OFFSET) &
			XCAN_ECR_REC_MASK) >> XCAN_ESR_REC_SHIFT);
	status = priv->read_reg(priv, XCAN_SR_OFFSET);

	if (isr & XCAN_IXR_BSOFF_MASK) {
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		/* Leave device in Config Mode in bus-off state */
		priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);
		can_bus_off(ndev);
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;
	} else if ((status & XCAN_SR_ESTAT_MASK) == XCAN_SR_ESTAT_MASK) {
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		priv->can.can_stats.error_passive++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = (rxerr > 127) ?
					CAN_ERR_CRTL_RX_PASSIVE :
					CAN_ERR_CRTL_TX_PASSIVE;
			cf->data[6] = txerr;
			cf->data[7] = rxerr;
		}
	} else if (status & XCAN_SR_ERRWRN_MASK) {
		priv->can.state = CAN_STATE_ERROR_WARNING;
		priv->can.can_stats.error_warning++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= (txerr > rxerr) ?
					CAN_ERR_CRTL_TX_WARNING :
					CAN_ERR_CRTL_RX_WARNING;
			cf->data[6] = txerr;
			cf->data[7] = rxerr;
		}
	}

	/* Check for Arbitration lost interrupt */
	if (isr & XCAN_IXR_ARBLST_MASK) {
		priv->can.can_stats.arbitration_lost++;
		if (skb) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] = CAN_ERR_LOSTARB_UNSPEC;
		}
	}

	/* Check for RX FIFO Overflow interrupt */
	if (isr & XCAN_IXR_RXOFLW_MASK) {
		stats->rx_over_errors++;
		stats->rx_errors++;
		xcan_chip_stop(ndev);
		xcan_chip_start(ndev);
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
		}
	}

	/* Check for error interrupt */
	if (isr & XCAN_IXR_ERROR_MASK) {
		if (skb)
			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

		/* Check for Ack error interrupt */
		if (err_status & XCAN_ESR_ACKER_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_ACK;
				cf->data[3] = CAN_ERR_PROT_LOC_ACK;
			}
		}

		/* Check for Bit error interrupt */
		if (err_status & XCAN_ESR_BERR_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_BIT;
			}
		}

		/* Check for Stuff error interrupt */
		if (err_status & XCAN_ESR_STER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_STUFF;
			}
		}

		/* Check for Form error interrupt */
		if (err_status & XCAN_ESR_FMER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_FORM;
			}
		}

		/* Check for CRC error interrupt */
		if (err_status & XCAN_ESR_CRCER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
			}
		}
		if (priv->quirks & CANFD_SUPPORT) {
			/* Check for Fast Bit error interrupt */
			if (err_status & XCAN_ESR_F_BERR_MASK) {
				stats->tx_errors++;
				if (skb) {
					cf->can_id |= CAN_ERR_PROT;
					cf->data[2] = CAN_ERR_PROT_BIT;
				}
			}
			/* Check for Stuff error interrupt */
			if (err_status & XCAN_ESR_F_STER_MASK) {
				stats->rx_errors++;
				if (skb) {
					cf->can_id |= CAN_ERR_PROT;
					cf->data[2] = CAN_ERR_PROT_STUFF;
				}
			}
			/* Check for Fast Form error interrupt */
			if (err_status & XCAN_ESR_F_FMER_MASK) {
				stats->rx_errors++;
				if (skb) {
					cf->can_id |= CAN_ERR_PROT;
					cf->data[2] = CAN_ERR_PROT_FORM;
				}
			}
			if (err_status & XCAN_ESR_F_CRCER_MASK) {
				stats->rx_errors++;
				if (skb) {
					cf->can_id |= CAN_ERR_PROT;
					priv->can.can_stats.bus_error++;
				}
			}
		}
		priv->can.can_stats.bus_error++;
	}

	if (skb) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}

	netdev_dbg(ndev, "%s: error status register:0x%x\n",
		   __func__, priv->read_reg(priv, XCAN_ESR_OFFSET));
}

/**
 * xcan_state_interrupt - It will check the state of the CAN device
 * @ndev:	net_device pointer
 * @isr:	interrupt status register value
 *
 * This will checks the state of the CAN device
 * and puts the device into appropriate state.
 */
static void xcan_state_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);

	/* Check for Sleep interrupt if set put CAN device in sleep state */
	if (isr & XCAN_IXR_SLP_MASK)
		priv->can.state = CAN_STATE_SLEEPING;

	/* Check for Wake up interrupt if set put CAN device in Active state */
	if (isr & XCAN_IXR_WKUP_MASK)
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
}

/**
 * xcan_rx_poll - Poll routine for rx packets (NAPI)
 * @napi:	napi structure pointer
 * @quota:	Max number of rx packets to be processed.
 *
 * This is the poll routine for rx part.
 * It will process the packets maximux quota value.
 *
 * Return: number of packets received
 */
static int xcan_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 isr, ier;
	int work_done = 0, rx_bit_mask;

	isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	rx_bit_mask = ((priv->quirks & CANFD_SUPPORT) ?
			XCAN_IXR_RXOK_MASK : XCAN_IXR_RXNEMP_MASK);
	while ((isr & rx_bit_mask) && (work_done < quota)) {
		if (rx_bit_mask & XCAN_IXR_RXOK_MASK)
			work_done += xcanfd_rx(ndev);
		else
			work_done += xcan_rx(ndev);
		priv->write_reg(priv, XCAN_ICR_OFFSET, rx_bit_mask);
		isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	}

	if (work_done)
		can_led_event(ndev, CAN_LED_EVENT_RX);

	if (work_done < quota) {
		napi_complete(napi);
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier |= rx_bit_mask;
		priv->write_reg(priv, XCAN_IER_OFFSET, ier);
	}
	return work_done;
}

/**
 * xcan_tx_interrupt - Tx Done Isr
 * @ndev:	net_device pointer
 * @isr:	Interrupt status register value
 */
static void xcan_tx_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;

	while ((priv->tx_head - priv->tx_tail > 0) &&
	       (isr & XCAN_IXR_TXOK_MASK)) {
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXOK_MASK);
		can_get_echo_skb(ndev, priv->tx_tail %
					priv->tx_max);
		priv->tx_tail++;
		stats->tx_packets++;
		isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	}
	can_led_event(ndev, CAN_LED_EVENT_TX);
	netif_wake_queue(ndev);
}

/**
 * xcan_interrupt - CAN Isr
 * @irq:	irq number
 * @dev_id:	device id poniter
 *
 * This is the xilinx CAN Isr. It checks for the type of interrupt
 * and invokes the corresponding ISR.
 *
 * Return:
 * IRQ_NONE - If CAN device is in sleep mode, IRQ_HANDLED otherwise
 */
static irqreturn_t xcan_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 isr, ier, rx_bit_mask;

	/* Get the interrupt status from Xilinx CAN */
	isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	if (!isr)
		return IRQ_NONE;

	/* Check for the type of interrupt and Processing it */
	if (isr & (XCAN_IXR_SLP_MASK | XCAN_IXR_WKUP_MASK)) {
		if (isr & XCAN_IXR_SLP_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_SLP_MASK);
		if (isr & XCAN_IXR_WKUP_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_WKUP_MASK);
		xcan_state_interrupt(ndev, isr);
	}

	/* Check for Tx interrupt and Processing it */
	if (isr & XCAN_IXR_TXOK_MASK)
		xcan_tx_interrupt(ndev, isr);

	/* Check for the type of error interrupt and Processing it */
	if (isr & (XCAN_IXR_ERROR_MASK | XCAN_IXR_RXOFLW_MASK |
			XCAN_IXR_BSOFF_MASK | XCAN_IXR_ARBLST_MASK)) {
		if (isr & XCAN_IXR_ERROR_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_ERROR_MASK);
		if (isr & XCAN_IXR_RXOFLW_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_RXOFLW_MASK);
		if (isr & XCAN_IXR_BSOFF_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_BSOFF_MASK);
		if (isr & XCAN_IXR_ARBLST_MASK)
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_ARBLST_MASK);

		xcan_err_interrupt(ndev, isr);
	}
	if (priv->quirks & CANFD_SUPPORT) {
		if (isr & (XCAN_IXR_RXMNF_MASK | XCAN_IXR_TXRRS_MASK |
			XCAN_IXR_PEE_MASK | XCAN_IXR_BSRD_MASK)) {
			priv->write_reg(priv, XCAN_ICR_OFFSET,
					(XCAN_IXR_RXMNF_MASK |
					 XCAN_IXR_TXRRS_MASK |
					XCAN_IXR_PEE_MASK |
					XCAN_IXR_BSRD_MASK));
			xcan_err_interrupt(ndev, isr);
		}
	}
	/* Check for the type of receive interrupt and Processing it */
	rx_bit_mask = ((priv->quirks & CANFD_SUPPORT) ?
			XCAN_IXR_RXOK_MASK : XCAN_IXR_RXNEMP_MASK);
	if (isr & rx_bit_mask) {
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier &= ~(rx_bit_mask);
		priv->write_reg(priv, XCAN_IER_OFFSET, ier);
		napi_schedule(&priv->napi);
	}
	return IRQ_HANDLED;
}

/**
 * xcan_chip_stop - Driver stop routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the drivers stop routine. It will disable the
 * interrupts and put the device into configuration mode.
 */
static void xcan_chip_stop(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 ier, intr_all = 0;

	/* Disable interrupts and leave the can in configuration mode */
	ier = priv->read_reg(priv, XCAN_IER_OFFSET);
	if (priv->quirks & CANFD_SUPPORT) {
		intr_all = XCAN_INTR_ALL | XCAN_IXR_PEE_MASK |
				XCAN_IXR_BSRD_MASK | XCAN_IXR_RXMNF_MASK |
				XCAN_IXR_TXRRS_MASK | XCAN_IXR_RXOK_MASK;
	} else {
		intr_all = XCAN_INTR_ALL | XCAN_IXR_RXNEMP_MASK;
	}

	ier &= ~intr_all;
	priv->write_reg(priv, XCAN_IER_OFFSET, ier);
	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);
	priv->can.state = CAN_STATE_STOPPED;
}

/**
 * xcan_open - Driver open routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver open routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_open(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		return ret;
	}

	ret = request_irq(ndev->irq, xcan_interrupt, priv->irq_flags,
			  ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "irq allocation for CAN failed\n");
		goto err;
	}

	/* Set chip into reset mode */
	ret = set_reset_mode(ndev);
	if (ret < 0) {
		netdev_err(ndev, "mode resetting failed!\n");
		goto err_irq;
	}

	/* Common open */
	ret = open_candev(ndev);
	if (ret)
		goto err_irq;

	ret = xcan_chip_start(ndev);
	if (ret < 0) {
		netdev_err(ndev, "xcan_chip_start failed!\n");
		goto err_candev;
	}

	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;

err_candev:
	close_candev(ndev);
err_irq:
	free_irq(ndev->irq, ndev);
err:
	pm_runtime_put(priv->dev);

	return ret;
}

/**
 * xcan_close - Driver close routine
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0 always
 */
static int xcan_close(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	xcan_chip_stop(ndev);
	free_irq(ndev->irq, ndev);
	close_candev(ndev);

	can_led_event(ndev, CAN_LED_EVENT_STOP);
	pm_runtime_put(priv->dev);

	return 0;
}

/**
 * xcan_get_berr_counter - error counter routine
 * @ndev:	Pointer to net_device structure
 * @bec:	Pointer to can_berr_counter structure
 *
 * This is the driver error counter routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_get_berr_counter(const struct net_device *ndev,
				 struct can_berr_counter *bec)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		return ret;
	}

	bec->txerr = priv->read_reg(priv, XCAN_ECR_OFFSET) & XCAN_ECR_TEC_MASK;
	bec->rxerr = ((priv->read_reg(priv, XCAN_ECR_OFFSET) &
			XCAN_ECR_REC_MASK) >> XCAN_ESR_REC_SHIFT);

	pm_runtime_put(priv->dev);

	return 0;
}

static const struct net_device_ops xcan_netdev_ops = {
	.ndo_open	= xcan_open,
	.ndo_stop	= xcan_close,
	.ndo_start_xmit	= xcan_start_xmit,
	.ndo_change_mtu	= can_change_mtu,
};

/**
 * xcan_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_suspend(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);

	if (!device_may_wakeup(dev)) {
		if (netif_running(netdev))
			xcan_close(netdev);
		return pm_runtime_force_suspend(dev);
	}

	return 0;
}

/**
 * xcan_resume - Resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_resume(struct device *dev)
{
	int ret;
	struct net_device *netdev = dev_get_drvdata(dev);

	if (!device_may_wakeup(dev)) {
		ret = pm_runtime_force_resume(dev);
		if (netif_running(netdev))
			xcan_open(netdev);
		return ret;
	}

	return 0;
}

/**
 * xcan_runtime_suspend - Runtime suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 always
 */
static int __maybe_unused xcan_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct xcan_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
	}

	priv->write_reg(priv, XCAN_MSR_OFFSET, XCAN_MSR_SLEEP_MASK);
	priv->can.state = CAN_STATE_SLEEPING;

	clk_disable_unprepare(priv->bus_clk);
	clk_disable_unprepare(priv->can_clk);

	return 0;
}

/**
 * xcan_runtime_resume - Runtime resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;
	u32 isr, status;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}
	ret = clk_prepare_enable(priv->can_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		clk_disable_unprepare(priv->bus_clk);
		return ret;
	}

	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);
	isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	status = priv->read_reg(priv, XCAN_SR_OFFSET);

	if (netif_running(ndev)) {
		if (isr & XCAN_IXR_BSOFF_MASK) {
			priv->can.state = CAN_STATE_BUS_OFF;
			priv->write_reg(priv, XCAN_SRR_OFFSET,
					XCAN_SRR_RESET_MASK);
		} else if ((status & XCAN_SR_ESTAT_MASK) ==
					XCAN_SR_ESTAT_MASK) {
			priv->can.state = CAN_STATE_ERROR_PASSIVE;
		} else if (status & XCAN_SR_ERRWRN_MASK) {
			priv->can.state = CAN_STATE_ERROR_WARNING;
		} else {
			priv->can.state = CAN_STATE_ERROR_ACTIVE;
		}
		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

static const struct dev_pm_ops xcan_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xcan_suspend, xcan_resume)
	SET_RUNTIME_PM_OPS(xcan_runtime_suspend, xcan_runtime_resume, NULL)
};

static const struct xcan_platform_data xcan_def = {
	.quirks = CANFD_SUPPORT,
};

/* Match table for OF platform binding */
static const struct of_device_id xcan_of_match[] = {
	{ .compatible = "xlnx,zynq-can-1.0", },
	{ .compatible = "xlnx,axi-can-1.00.a", },
	{ .compatible = "xlnx,canfd-1.0", .data = &xcan_def },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, xcan_of_match);

/**
 * xcan_probe - Platform registration call
 * @pdev:	Handle to the platform device structure
 *
 * This function does all the memory allocation and registration for the CAN
 * device.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_probe(struct platform_device *pdev)
{
	struct resource *res; /* IO mem resources */
	struct net_device *ndev;
	struct xcan_priv *priv;
	const struct of_device_id *match;
	void __iomem *addr;
	int ret, rx_max, tx_max;

	/* Get the virtual base address for the device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto err;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "tx-fifo-depth", &tx_max);
	if (ret < 0)
		goto err;

	ret = of_property_read_u32(pdev->dev.of_node, "rx-fifo-depth", &rx_max);
	if (ret < 0)
		goto err;

	/* Create a CAN device instance */
	ndev = alloc_candev(sizeof(struct xcan_priv), tx_max);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);

	match = of_match_node(xcan_of_match, pdev->dev.of_node);
	if (match && match->data) {
		const struct xcan_platform_data *data = match->data;

		priv->quirks = data->quirks;
	}

	priv->dev = &pdev->dev;
	priv->can.bittiming_const = &xcan_bittiming_const;
	priv->can.do_set_mode = xcan_do_set_mode;
	priv->can.do_get_berr_counter = xcan_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
					CAN_CTRLMODE_BERR_REPORTING;
	if (priv->quirks & CANFD_SUPPORT) {
		priv->can.data_bittiming_const = &xcan_data_bittiming_const;
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_FD;
		xcan_bittiming_const.tseg1_max = 64;
		xcan_bittiming_const.tseg2_max = 16;
		xcan_bittiming_const.sjw_max = 16;
	}
	priv->reg_base = addr;
	priv->tx_max = tx_max;
	priv->tx_head = 0;
	priv->tx_tail = 0;

	/* Get IRQ for the device */
	ndev->irq = platform_get_irq(pdev, 0);
	ndev->flags |= IFF_ECHO;	/* We support local echo */

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &xcan_netdev_ops;

	/* Getting the CAN can_clk info */
	priv->can_clk = devm_clk_get(&pdev->dev, "can_clk");
	if (IS_ERR(priv->can_clk)) {
		dev_err(&pdev->dev, "Device clock not found.\n");
		ret = PTR_ERR(priv->can_clk);
		goto err_free;
	}
	/* Check for type of CAN device */
	if (of_device_is_compatible(pdev->dev.of_node,
				    "xlnx,zynq-can-1.0")) {
		priv->bus_clk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(priv->bus_clk)) {
			dev_err(&pdev->dev, "bus clock not found\n");
			ret = PTR_ERR(priv->bus_clk);
			goto err_free;
		}
	} else {
		priv->bus_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
		if (IS_ERR(priv->bus_clk)) {
			dev_err(&pdev->dev, "bus clock not found\n");
			ret = PTR_ERR(priv->bus_clk);
			goto err_free;
		}
	}

	priv->write_reg = xcan_write_reg_le;
	priv->read_reg = xcan_read_reg_le;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable clock.\n");
		goto err_free;
	}

	ret = clk_prepare_enable(priv->can_clk);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable clock.\n");
		goto err_clk;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (priv->read_reg(priv, XCAN_SR_OFFSET) != XCAN_SR_CONFIG_MASK) {
		priv->write_reg = xcan_write_reg_be;
		priv->read_reg = xcan_read_reg_be;
	}

	priv->can.clock.freq = clk_get_rate(priv->can_clk);

	netif_napi_add(ndev, &priv->napi, xcan_rx_poll, rx_max);

	ret = register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register failed (err=%d)\n", ret);
		goto err_disableclks;
	}

	devm_can_led_init(ndev);

	pm_runtime_put(&pdev->dev);

	netdev_dbg(ndev, "reg_base=0x%p irq=%d clock=%d, tx fifo depth:%d\n",
		   priv->reg_base, ndev->irq, priv->can.clock.freq,
		   priv->tx_max);

	return 0;

err_disableclks:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_disable_unprepare(priv->can_clk);
err_clk:
	clk_disable_unprepare(priv->bus_clk);
err_free:
	free_candev(ndev);
err:
	return ret;
}

/**
 * xcan_remove - Unregister the device after releasing the resources
 * @pdev:	Handle to the platform device structure
 *
 * This function frees all the resources allocated to the device.
 * Return: 0 always
 */
static int xcan_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct xcan_priv *priv = netdev_priv(ndev);

	unregister_candev(ndev);

	if (!pm_runtime_suspended(&pdev->dev)) {
		clk_disable_unprepare(priv->bus_clk);
		clk_disable_unprepare(priv->can_clk);
	}

	pm_runtime_disable(&pdev->dev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);

	return 0;
}

static struct platform_driver xcan_driver = {
	.probe = xcan_probe,
	.remove	= xcan_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.pm = &xcan_dev_pm_ops,
		.of_match_table	= xcan_of_match,
	},
};

module_platform_driver(xcan_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx CAN interface");

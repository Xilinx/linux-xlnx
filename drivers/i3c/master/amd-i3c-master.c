// SPDX-License-Identifier: GPL-2.0
/*
 * I3C master driver for the AMD I3C controller.
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#include "../internals.h"

#define XI3C_VERSION_OFFSET			0x00	/* Version Register */
#define XI3C_RESET_OFFSET			0x04	/* Soft Reset Register */
#define XI3C_CR_OFFSET				0x08	/* Control Register */
#define XI3C_ADDRESS_OFFSET			0x0C	/* Target Address Register */
#define XI3C_SR_OFFSET				0x10	/* Status Register */
#define XI3C_CMD_FIFO_OFFSET			0x20	/* I3C Command FIFO Register */
#define XI3C_WR_FIFO_OFFSET			0x24	/* I3C Write Data FIFO Register */
#define XI3C_RD_FIFO_OFFSET			0x28	/* I3C Read Data FIFO Register */
#define XI3C_RESP_STATUS_FIFO_OFFSET		0x2C	/* I3C Response status FIFO Register */
#define XI3C_FIFO_LVL_STATUS_OFFSET		0x30	/* I3C CMD & WR FIFO LVL Register */
#define XI3C_FIFO_LVL_STATUS_1_OFFSET		0x34	/* I3C RESP & RD FIFO LVL Register */
#define XI3C_SCL_HIGH_TIME_OFFSET		0x38	/* I3C SCL HIGH Register */
#define XI3C_SCL_LOW_TIME_OFFSET		0x3C	/* I3C SCL LOW  Register */
#define XI3C_SDA_HOLD_TIME_OFFSET		0x40	/* I3C SDA HOLD Register */
#define XI3C_TSU_START_OFFSET			0x48	/* I3C START SETUP Register  */
#define XI3C_THD_START_OFFSET			0x4C	/* I3C START HOLD Register */
#define XI3C_TSU_STOP_OFFSET			0x50	/* I3C STOP Setup Register  */
#define XI3C_OD_SCL_HIGH_TIME_OFFSET		0x54	/* I3C OD SCL HIGH Register */
#define XI3C_OD_SCL_LOW_TIME_OFFSET		0x58	/* I3C OD SCL LOW  Register */
#define XI3C_PID0_OFFSET			0x6C	/* LSB 4 bytes of the PID */
#define XI3C_PID1_BCR_DCR			0x70	/* MSB 2 bytes of the PID, BCR and DCR */

#define XI3C_CR_EN_MASK				BIT(0)	/* Core Enable */
#define XI3C_CR_RESUME_MASK			BIT(2)	/* Core Resume */
#define XI3C_SR_RESP_NOT_EMPTY_MASK		BIT(4)	/* Resp Fifo not empty status mask */
#define XI3C_RD_FIFO_NOT_EMPTY_MASK		BIT(15)	/* Read Fifo not empty status mask */

#define XI3C_BCR_MASK				GENMASK(23, 16)
#define XI3C_DCR_MASK				GENMASK(31, 24)
#define XI3C_PID_MASK				GENMASK_ULL(63, 16)
#define XI3C_SCL_HIGH_TIME_MASK			GENMASK(17, 0)
#define XI3C_SCL_LOW_TIME_MASK			GENMASK(17, 0)
#define XI3C_SDA_HOLD_TIME_MASK			GENMASK(17, 0)
#define XI3C_TSU_START_MASK			GENMASK(17, 0)
#define XI3C_THD_START_MASK			GENMASK(17, 0)
#define XI3C_TSU_STOP_MASK			GENMASK(17, 0)
#define XI3C_REV_NUM_MASK			GENMASK(15, 8)
#define XI3C_PID1_MASK				GENMASK(15, 0)
#define XI3C_WR_FIFO_LEVEL_MASK			GENMASK(15, 0)
#define XI3C_CMD_LEN_MASK			GENMASK(11, 0)
#define XI3C_RESP_CODE_MASK			GENMASK(8, 5)
#define XI3C_ADDR_MASK				GENMASK(6, 0)
#define XI3C_CMD_TYPE_MASK			GENMASK(3, 0)
#define XI3C_CMD_TID_MASK			GENMASK(3, 0)
#define XI3C_FIFOS_RST_MASK			GENMASK(4, 1)

#define XI3C_OD_TLOW_NS				500000
#define XI3C_OD_THIGH_NS			41000
#define XI3C_I2C_TCASMIN_NS			600000
#define XI3C_TCASMIN_NS				260000
#define XI3C_MAXDATA_LENGTH			4095
#define XI3C_MAX_DEVS				32
#define XI3C_DAA_SLAVEINFO_READ_BYTECOUNT	8

#define XI3C_I2C_MODE				0
#define XI3C_I2C_TID				0
#define XI3C_SDR_MODE				1
#define XI3C_SDR_TID				1

#define XI3C_WORD_LEN				4

/* timeout waiting for the controller finish transfers */
#define XI3C_XFER_TIMEOUT_MS			100000
#define XI3C_XFER_TIMEOUT_JIFFIES		(msecs_to_jiffies(XI3C_XFER_TIMEOUT_MS))

#define xi3c_getrevisionnumber(master)						\
	(FIELD_GET(XI3C_REV_NUM_MASK,						\
		   ioread32((master)->membase + XI3C_VERSION_OFFSET)))

#define xi3c_wrfifolevel(master)						\
	(ioread32((master)->membase + XI3C_FIFO_LVL_STATUS_OFFSET) &		\
	 XI3C_WR_FIFO_LEVEL_MASK)

#define xi3c_rdfifolevel(master)						\
	(ioread32((master)->membase + XI3C_FIFO_LVL_STATUS_1_OFFSET) &		\
	 XI3C_WR_FIFO_LEVEL_MASK)

#define xi3c_is_resp_available(master)						\
	(FIELD_GET(XI3C_SR_RESP_NOT_EMPTY_MASK,					\
		   ioread32((master)->membase + XI3C_SR_OFFSET)))

struct xi3c_cmd {
	void *tx_buf;
	void *rx_buf;
	u16 tx_len;
	u16 rx_len;
	u8 addr;
	u8 type;
	u8 tid;
	bool rnw;
	bool is_daa;
	bool continued;
};

struct xi3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int ncmds;
	struct xi3c_cmd cmds[] __counted_by(ncmds);
};

/**
 * struct xi3c_master - I3C Master structure
 * @base: I3C master controller
 * @dev: Pointer to device structure
 * @xferqueue: Transfer queue structure
 * @xferqueue.list: List member
 * @xferqueue.cur: Current ongoing transfer
 * @xferqueue.lock: Queue lock
 * @membase: Memory base of the HW registers
 * @pclk: Input clock
 * @lock: Transfer lock
 * @daa: daa structure
 * @daa.addrs: Slave addresses array
 * @daa.index: Slave device index
 */
struct xi3c_master {
	struct i3c_master_controller base;
	struct device *dev;
	struct {
		struct list_head list;
		struct xi3c_xfer *cur;
		/* Queue lock */
		spinlock_t lock;
	} xferqueue;
	void __iomem *membase;
	struct clk *pclk;
	/* Transfer lock */
	struct mutex lock;
	struct {
		u8 addrs[XI3C_MAX_DEVS];
		u8 index;
	} daa;
};

static inline struct xi3c_master *
to_xi3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct xi3c_master, base);
}

static int xi3c_get_response(struct xi3c_master *master)
{
	u32 resp_reg, response_data;
	int ret;

	ret = readl_poll_timeout(master->membase + XI3C_SR_OFFSET,
				 resp_reg,
				 resp_reg & XI3C_SR_RESP_NOT_EMPTY_MASK,
				 0, XI3C_XFER_TIMEOUT_MS);
	if (ret) {
		dev_err(master->dev, "XI3C response timeout\n");
		return ret;
	}

	response_data = ioread32(master->membase + XI3C_RESP_STATUS_FIFO_OFFSET);

	/* Return response code */
	return FIELD_GET(XI3C_RESP_CODE_MASK, response_data);
}

static void xi3c_master_write_to_cmdfifo(struct xi3c_master *master,
					 struct xi3c_cmd *cmd, u16 len)
{
	u32 transfer_cmd = 0;
	u8 addr;

	addr = ((cmd->addr & XI3C_ADDR_MASK) << 1) | (cmd->rnw & BIT(0));

	transfer_cmd = cmd->type & XI3C_CMD_TYPE_MASK;
	transfer_cmd |= (u32)(!cmd->continued) << 4;
	transfer_cmd |= (u32)(addr) << 8;
	transfer_cmd |= (u32)(cmd->tid & XI3C_CMD_TID_MASK) << 28;

	/*
	 * For dynamic addressing, an additional 1-byte length must be added
	 * to the command FIFO to account for the address present in the TX FIFO
	 */
	if (cmd->is_daa) {
		i3c_writel_fifo(master->membase + XI3C_WR_FIFO_OFFSET,
				(u8 *)cmd->tx_buf, cmd->tx_len, I3C_FIFO_BIG_ENDIAN);

		len++;
		master->daa.index++;
	}

	transfer_cmd |= (u32)(len & XI3C_CMD_LEN_MASK) << 16;
	iowrite32(transfer_cmd, master->membase + XI3C_CMD_FIFO_OFFSET);
}

static inline void xi3c_master_enable(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) | XI3C_CR_EN_MASK,
		  master->membase + XI3C_CR_OFFSET);
}

static inline void xi3c_master_disable(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) & (~XI3C_CR_EN_MASK),
		  master->membase + XI3C_CR_OFFSET);
}

static inline void xi3c_master_resume(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) |
		  XI3C_CR_RESUME_MASK, master->membase + XI3C_CR_OFFSET);
}

static void xi3c_master_reset_fifos(struct xi3c_master *master)
{
	u32 data;

	/* Reset fifos */
	data = ioread32(master->membase + XI3C_RESET_OFFSET);
	data |= XI3C_FIFOS_RST_MASK;
	iowrite32(data, master->membase + XI3C_RESET_OFFSET);
	ioread32(master->membase + XI3C_RESET_OFFSET);
	udelay(10);
	data &= ~XI3C_FIFOS_RST_MASK;
	iowrite32(data, master->membase + XI3C_RESET_OFFSET);
	ioread32(master->membase + XI3C_RESET_OFFSET);
	udelay(10);
}

static inline void xi3c_master_init(struct xi3c_master *master)
{
	/* Reset fifos */
	xi3c_master_reset_fifos(master);

	/* Enable controller */
	xi3c_master_enable(master);
}

static inline void xi3c_master_reinit(struct xi3c_master *master)
{
	/* Reset fifos */
	xi3c_master_reset_fifos(master);

	/* Resume controller */
	xi3c_master_resume(master);
}

static struct xi3c_xfer *
xi3c_master_alloc_xfer(struct xi3c_master *master, unsigned int ncmds)
{
	struct xi3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void xi3c_master_rd_from_rx_fifo(struct xi3c_master *master,
					struct xi3c_cmd *cmd)
{
	u16 rx_data_available;
	u16 len;

	rx_data_available = xi3c_rdfifolevel(master);
	len = rx_data_available * XI3C_WORD_LEN;

	if (len) {
		i3c_readl_fifo(master->membase + XI3C_RD_FIFO_OFFSET, (u8 *)cmd->rx_buf,
			       len, I3C_FIFO_BIG_ENDIAN);

		cmd->rx_buf = (u8 *)cmd->rx_buf + len;
		cmd->rx_len -= len;
	}
}

static int xi3c_master_read(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	unsigned long timeout;
	u32 status_reg;
	int ret;

	if (!cmd->rx_buf || cmd->rx_len > XI3C_MAXDATA_LENGTH)
		return -EINVAL;

	/* Fill command fifo */
	xi3c_master_write_to_cmdfifo(master, cmd, cmd->rx_len);

	ret = readl_poll_timeout(master->membase + XI3C_SR_OFFSET,
				 status_reg,
				 status_reg & XI3C_RD_FIFO_NOT_EMPTY_MASK,
				 0, XI3C_XFER_TIMEOUT_MS);
	if (ret) {
		if (cmd->is_daa) {
			cmd->is_daa = false;
			ret = I3C_ERROR_M2;
		} else {
			dev_err(master->dev, "XI3C read timeout\n");
		}
		return ret;
	}

	timeout = jiffies + XI3C_XFER_TIMEOUT_JIFFIES;

	/* Read data from rx fifo */
	while (cmd->rx_len > 0 && !xi3c_is_resp_available(master)) {
		if (time_after(jiffies, timeout)) {
			dev_err(master->dev, "XI3C read timeout\n");
			return -EIO;
		}
		xi3c_master_rd_from_rx_fifo(master, cmd);
	}

	/* Read remaining data */
	xi3c_master_rd_from_rx_fifo(master, cmd);

	return 0;
}

static void xi3c_master_wr_to_tx_fifo(struct xi3c_master *master,
				      struct xi3c_cmd *cmd)
{
	u16 wrfifo_space;
	u16 len;

	wrfifo_space = xi3c_wrfifolevel(master);
	if (cmd->tx_len > wrfifo_space * XI3C_WORD_LEN)
		len = wrfifo_space * XI3C_WORD_LEN;
	else
		len = cmd->tx_len;

	if (len) {
		i3c_writel_fifo(master->membase + XI3C_WR_FIFO_OFFSET, (u8 *)cmd->tx_buf,
				len, I3C_FIFO_BIG_ENDIAN);

		cmd->tx_buf = (u8 *)cmd->tx_buf + len;
		cmd->tx_len -= len;
	}
}

static int xi3c_master_write(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	unsigned long timeout;
	u16 cmd_len;

	cmd_len = cmd->tx_len;
	if (!cmd->tx_buf || cmd->tx_len > XI3C_MAXDATA_LENGTH)
		return -EINVAL;

	/* Fill Tx fifo */
	xi3c_master_wr_to_tx_fifo(master, cmd);

	/* Write to command fifo */
	xi3c_master_write_to_cmdfifo(master, cmd, cmd_len);

	timeout = jiffies + XI3C_XFER_TIMEOUT_JIFFIES;
	/* Fill if any remaining data to tx fifo */
	while (cmd->tx_len > 0 && !xi3c_is_resp_available(master)) {
		if (time_after(jiffies, timeout)) {
			dev_err(master->dev, "XI3C write timeout\n");
			return -EIO;
		}

		xi3c_master_wr_to_tx_fifo(master, cmd);
	}

	return 0;
}

static int xi3c_master_xfer(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	int ret;

	if (cmd->rnw)
		ret = xi3c_master_read(master, cmd);
	else
		ret = xi3c_master_write(master, cmd);

	if (ret < 0)
		goto err_xfer_out;

	ret = xi3c_get_response(master);
	if (ret)
		goto err_xfer_out;

	return 0;

err_xfer_out:
	xi3c_master_reinit(master);
	return ret;
}

static void xi3c_master_dequeue_xfer_locked(struct xi3c_master *master,
					    struct xi3c_xfer *xfer)
{
	if (master->xferqueue.cur == xfer)
		master->xferqueue.cur = NULL;
	else
		list_del_init(&xfer->node);
}

static void xi3c_master_dequeue_xfer(struct xi3c_master *master,
				     struct xi3c_xfer *xfer)
{
	guard(spinlock_irqsave)(&master->xferqueue.lock);

	xi3c_master_dequeue_xfer_locked(master, xfer);
}

static void xi3c_master_start_xfer_locked(struct xi3c_master *master)
{
	struct xi3c_xfer *xfer = master->xferqueue.cur;
	int ret = 0, i;

	if (!xfer)
		return;

	for (i = 0; i < xfer->ncmds; i++) {
		struct xi3c_cmd *cmd = &xfer->cmds[i];

		ret = xi3c_master_xfer(master, cmd);
		if (ret)
			break;
	}

	xfer->ret = ret;
	complete(&xfer->comp);

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct xi3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	xi3c_master_start_xfer_locked(master);
}

static inline void xi3c_master_enqueue_xfer(struct xi3c_master *master,
					    struct xi3c_xfer *xfer)
{
	init_completion(&xfer->comp);

	guard(spinlock_irqsave)(&master->xferqueue.lock);

	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		xi3c_master_start_xfer_locked(master);
	}
}

static inline int xi3c_master_common_xfer(struct xi3c_master *master,
					  struct xi3c_xfer *xfer)
{
	int ret, time_left;

	guard(mutex)(&master->lock);

	xi3c_master_enqueue_xfer(master, xfer);
	time_left = wait_for_completion_timeout(&xfer->comp,
						XI3C_XFER_TIMEOUT_JIFFIES);
	if (!time_left)
		ret = -ETIMEDOUT;
	else
		ret = xfer->ret;

	if (ret)
		xi3c_master_dequeue_xfer(master, xfer);

	return ret;
}

static int xi3c_master_do_daa(struct i3c_master_controller *m)
{
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_cmd *daa_cmd;
	struct xi3c_xfer *xfer __free(kfree);
	u8 pid_bufs[XI3C_MAX_DEVS][8];
	u8 *pid_buf;
	u8 data, last_addr = 0;
	int addr, ret, i;

	xfer = xi3c_master_alloc_xfer(master, 1);
	if (!xfer) {
		ret = -ENOMEM;
		goto err_daa;
	}

	for (i = 0; i < XI3C_MAX_DEVS; i++) {
		addr = i3c_master_get_free_addr(m, last_addr + 1);
		if (addr < 0) {
			ret = -ENOSPC;
			goto err_daa;
		}
		master->daa.addrs[i] = (u8)addr;
		last_addr = (u8)addr;
	}

	/* Fill ENTDAA CCC */
	data = I3C_CCC_ENTDAA;
	daa_cmd = &xfer->cmds[0];
	daa_cmd->addr = I3C_BROADCAST_ADDR;
	daa_cmd->rnw = 0;
	daa_cmd->tx_buf = &data;
	daa_cmd->tx_len = 1;
	daa_cmd->type = XI3C_SDR_MODE;
	daa_cmd->tid = XI3C_SDR_TID;
	daa_cmd->continued = true;

	ret = xi3c_master_common_xfer(master, xfer);
	/* DAA always finishes with CE2_ERROR or NACK_RESP */
	if (ret && ret != I3C_ERROR_M2) {
		goto err_daa;
	} else {
		if (ret && ret == I3C_ERROR_M2) {
			ret = 0;
			goto err_daa;
		}
	}

	master->daa.index = 0;

	while (true) {
		struct xi3c_cmd *cmd = &xfer->cmds[0];

		pid_buf = pid_bufs[master->daa.index];
		addr = (master->daa.addrs[master->daa.index] << 1) |
		       (u8)(!parity8(master->daa.addrs[master->daa.index]));

		cmd->tx_buf = (u8 *)&addr;
		cmd->tx_len = 1;
		cmd->addr = I3C_BROADCAST_ADDR;
		cmd->rnw = 1;
		cmd->rx_buf = pid_buf;
		cmd->rx_len = XI3C_DAA_SLAVEINFO_READ_BYTECOUNT;
		cmd->is_daa = true;
		cmd->type = XI3C_SDR_MODE;
		cmd->tid = XI3C_SDR_TID;
		cmd->continued = true;

		ret = xi3c_master_common_xfer(master, xfer);

		/* DAA always finishes with CE2_ERROR or NACK_RESP */
		if (ret && ret != I3C_ERROR_M2) {
			goto err_daa;
		} else {
			if (ret && ret == I3C_ERROR_M2) {
				xi3c_master_resume(master);
				master->daa.index--;
				ret = 0;
				break;
			}
		}
	}

	for (i = 0; i < master->daa.index; i++) {
		i3c_master_add_i3c_dev_locked(m, master->daa.addrs[i]);

		u64 data = FIELD_GET(XI3C_PID_MASK, get_unaligned_be64(pid_bufs[i]));

		dev_info(master->dev, "Client %d: PID: 0x%llx\n", i, data);
	}

	return 0;

err_daa:
	xi3c_master_reinit(master);
	return ret;
}

static bool
xi3c_master_supports_ccc_cmd(struct i3c_master_controller *master,
			     const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
		return true;
	default:
		return false;
	}
}

static int xi3c_master_send_bdcast_ccc_cmd(struct xi3c_master *master,
					   struct i3c_ccc_cmd *ccc)
{
	u16 xfer_len = ccc->dests[0].payload.len + 1;
	struct xi3c_xfer *xfer __free(kfree);
	struct xi3c_cmd *cmd;
	int ret;

	xfer = xi3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	u8 *buf __free(kfree) = kmalloc(xfer_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = ccc->id;
	memcpy(&buf[1], ccc->dests[0].payload.data, ccc->dests[0].payload.len);

	cmd = &xfer->cmds[0];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	cmd->tx_buf = buf;
	cmd->tx_len = xfer_len;
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = false;

	ret = xi3c_master_common_xfer(master, xfer);

	return ret;
}

static int xi3c_master_send_direct_ccc_cmd(struct xi3c_master *master,
					   struct i3c_ccc_cmd *ccc)
{
	struct xi3c_xfer *xfer __free(kfree);
	struct xi3c_cmd *cmd;
	int ret;

	xfer = xi3c_master_alloc_xfer(master, 2);
	if (!xfer)
		return -ENOMEM;

	/* Broadcasted message */
	cmd = &xfer->cmds[0];
	cmd->addr = I3C_BROADCAST_ADDR;
	cmd->rnw = 0;
	cmd->tx_buf = &ccc->id;
	cmd->tx_len = 1;
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = true;

	/* Directed message */
	cmd = &xfer->cmds[1];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	if (cmd->rnw) {
		cmd->rx_buf = ccc->dests[0].payload.data;
		cmd->rx_len = ccc->dests[0].payload.len;
	} else {
		cmd->tx_buf = ccc->dests[0].payload.data;
		cmd->tx_len = ccc->dests[0].payload.len;
	}
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = false;

	ret = xi3c_master_common_xfer(master, xfer);
	return ret;
}

static int xi3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				    struct i3c_ccc_cmd *cmd)
{
	struct xi3c_master *master = to_xi3c_master(m);
	bool broadcast = cmd->id < 0x80;

	if (broadcast)
		return xi3c_master_send_bdcast_ccc_cmd(master, cmd);

	return xi3c_master_send_direct_ccc_cmd(master, cmd);
}

static int xi3c_master_priv_xfers(struct i3c_dev_desc *dev,
				  struct i3c_priv_xfer *xfers,
				  int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_xfer *xfer __free(kfree);
	int i, ret;

	if (!nxfers)
		return 0;

	xfer = xi3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct xi3c_cmd *cmd = &xfer->cmds[i];

		cmd->addr = dev->info.dyn_addr;
		cmd->rnw = xfers[i].rnw;

		if (cmd->rnw) {
			cmd->rx_buf = xfers[i].data.in;
			cmd->rx_len = xfers[i].len;
		} else {
			cmd->tx_buf = (void *)xfers[i].data.out;
			cmd->tx_len = xfers[i].len;
		}

		cmd->type = XI3C_SDR_MODE;
		cmd->tid = XI3C_SDR_TID;
		cmd->continued = (i + 1) < nxfers;
	}

	ret = xi3c_master_common_xfer(master, xfer);
	return ret;
}

static int xi3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				 struct i2c_msg *xfers,
				 int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_xfer *xfer __free(kfree);
	int i, ret;

	if (!nxfers)
		return 0;

	xfer = xi3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct xi3c_cmd *cmd = &xfer->cmds[i];

		cmd->addr = xfers[i].addr & XI3C_ADDR_MASK;
		cmd->rnw = xfers[i].flags & I2C_M_RD;

		if (cmd->rnw) {
			cmd->rx_buf = xfers[i].buf;
			cmd->rx_len = xfers[i].len;
		} else {
			cmd->tx_buf = (void *)xfers[i].buf;
			cmd->tx_len = xfers[i].len;
		}

		cmd->type = XI3C_I2C_MODE;
		cmd->tid = XI3C_I2C_TID;
		cmd->continued = (i + 1) < nxfers;
	}

	ret = xi3c_master_common_xfer(master, xfer);
	return ret;
}

static int xi3c_clk_cfg(struct xi3c_master *master, unsigned long sclhz, u8 mode)
{
	unsigned long core_rate, core_periodns;
	u32 thigh, tlow, thold, odthigh, odtlow, tcasmin, tsustart, tsustop, thdstart;

	core_rate = clk_get_rate(master->pclk);
	if (!core_rate)
		return -EINVAL;

	core_periodns = DIV_ROUND_UP(1000000000, core_rate);

	thigh = DIV_ROUND_UP(core_rate, sclhz) >> 1;
	tlow = thigh;

	/* Hold time : 40% of tlow time */
	thold = (tlow * 4) / 10;

	/*
	 * For initial IP (revision number == 0), minimum data hold time is 5.
	 * For updated IP (revision number > 0), minimum data hold time is 6.
	 * Updated IP supports achieving high data rate with low reference
	 * frequency.
	 */
	if (xi3c_getrevisionnumber(master) == 0)
		thold = (thold < 5) ? 5 : thold;
	else
		thold = (thold < 6) ? 6 : thold;

	iowrite32((thigh - 2) & XI3C_SCL_HIGH_TIME_MASK,
		  master->membase + XI3C_SCL_HIGH_TIME_OFFSET);
	iowrite32((tlow - 2) & XI3C_SCL_LOW_TIME_MASK,
		  master->membase + XI3C_SCL_LOW_TIME_OFFSET);
	iowrite32((thold - 2) & XI3C_SDA_HOLD_TIME_MASK,
		  master->membase + XI3C_SDA_HOLD_TIME_OFFSET);

	if (!mode) {
		/* I2C */
		iowrite32((thigh - 2) & XI3C_SCL_HIGH_TIME_MASK,
			  master->membase + XI3C_OD_SCL_HIGH_TIME_OFFSET);
		iowrite32((tlow - 2) & XI3C_SCL_LOW_TIME_MASK,
			  master->membase + XI3C_OD_SCL_LOW_TIME_OFFSET);

		tcasmin = DIV_ROUND_UP(XI3C_I2C_TCASMIN_NS, core_periodns);
	} else {
		/* I3C */
		odtlow = DIV_ROUND_UP(XI3C_OD_TLOW_NS, core_periodns);
		odthigh = DIV_ROUND_UP(XI3C_OD_THIGH_NS, core_periodns);

		odtlow = (tlow < odtlow) ? odtlow : tlow;
		odthigh = (thigh > odthigh) ? odthigh : thigh;

		iowrite32((odthigh - 2) & XI3C_SCL_HIGH_TIME_MASK,
			  master->membase + XI3C_OD_SCL_HIGH_TIME_OFFSET);
		iowrite32((odtlow - 2) & XI3C_SCL_LOW_TIME_MASK,
			  master->membase + XI3C_OD_SCL_LOW_TIME_OFFSET);

		tcasmin = DIV_ROUND_UP(XI3C_TCASMIN_NS, core_periodns);
	}

	thdstart = (thigh > tcasmin) ? thigh : tcasmin;
	tsustart = (tlow > tcasmin) ? tlow : tcasmin;
	tsustop = (tlow > tcasmin) ? tlow : tcasmin;

	iowrite32((tsustart - 2) & XI3C_TSU_START_MASK,
		  master->membase + XI3C_TSU_START_OFFSET);
	iowrite32((thdstart - 2) & XI3C_THD_START_MASK,
		  master->membase + XI3C_THD_START_OFFSET);
	iowrite32((tsustop - 2) & XI3C_TSU_STOP_MASK,
		  master->membase + XI3C_TSU_STOP_OFFSET);

	return 0;
}

static int xi3c_master_bus_init(struct i3c_master_controller *m)
{
	struct xi3c_master *master = to_xi3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	unsigned long sclhz;
	u64 pid1_bcr_dcr;
	u8 mode;
	int ret;

	switch (bus->mode) {
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		mode = XI3C_I2C_MODE;
		sclhz = bus->scl_rate.i2c;
		break;
	case I3C_BUS_MODE_PURE:
		mode = XI3C_SDR_MODE;
		sclhz = bus->scl_rate.i3c;
		break;
	default:
		return -EINVAL;
	}

	ret = xi3c_clk_cfg(master, sclhz, mode);
	if (ret)
		return ret;

	/* Get an address for the master. */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	info.dyn_addr = (u8)ret;

	/* Write the dynamic address value to the address register. */
	iowrite32(info.dyn_addr, master->membase + XI3C_ADDRESS_OFFSET);

	/* Read PID, BCR and DCR values, and assign to i3c device info. */
	pid1_bcr_dcr = ioread32(master->membase + XI3C_PID1_BCR_DCR);
	info.pid = (((u64)(FIELD_GET(XI3C_PID1_MASK, pid1_bcr_dcr)) << 32) |
		    ioread32(master->membase + XI3C_PID0_OFFSET));
	info.bcr = (u8)FIELD_GET(XI3C_BCR_MASK, pid1_bcr_dcr);
	info.dcr = (u8)FIELD_GET(XI3C_DCR_MASK, pid1_bcr_dcr);

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	xi3c_master_init(master);

	return ret;
}

static void xi3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct xi3c_master *master = to_xi3c_master(m);

	xi3c_master_disable(master);
}

static const struct i3c_master_controller_ops xi3c_master_ops = {
	.bus_init = xi3c_master_bus_init,
	.bus_cleanup = xi3c_master_bus_cleanup,
	.do_daa = xi3c_master_do_daa,
	.supports_ccc_cmd = xi3c_master_supports_ccc_cmd,
	.send_ccc_cmd = xi3c_master_send_ccc_cmd,
	.priv_xfers = xi3c_master_priv_xfers,
	.i2c_xfers = xi3c_master_i2c_xfers,
};

static int xi3c_master_probe(struct platform_device *pdev)
{
	struct xi3c_master *master;
	int ret;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->membase))
		return PTR_ERR(master->membase);

	master->pclk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(master->pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(master->pclk),
				     "Failed to get and enable clock\n");

	master->dev = &pdev->dev;

	ret = devm_mutex_init(master->dev, &master->lock);
	if (ret)
		return ret;

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	platform_set_drvdata(pdev, master);

	return i3c_master_register(&master->base, &pdev->dev,
				   &xi3c_master_ops, false);
}

static void xi3c_master_remove(struct platform_device *pdev)
{
	struct xi3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);
}

static const struct of_device_id xi3c_master_of_ids[] = {
	{ .compatible = "xlnx,axi-i3c-1.0" },
	{ },
};

static struct platform_driver xi3c_master_driver = {
	.probe = xi3c_master_probe,
	.remove = xi3c_master_remove,
	.driver = {
		.name = "axi-i3c-master",
		.of_match_table = xi3c_master_of_ids,
	},
};
module_platform_driver(xi3c_master_driver);

MODULE_AUTHOR("Manikanta Guntupalli <manikanta.guntupalli@amd.com>");
MODULE_DESCRIPTION("AXI I3C master driver");
MODULE_LICENSE("GPL");

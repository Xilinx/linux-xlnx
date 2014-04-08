/*
 * Driver for the i2c controller on the Marvell line of host bridges
 * (e.g, gt642[46]0, mv643[46]0, mv644[46]0, and Orion SoC family).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mv643xx_i2c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>

#define MV64XXX_I2C_ADDR_ADDR(val)			((val & 0x7f) << 1)
#define MV64XXX_I2C_BAUD_DIV_N(val)			(val & 0x7)
#define MV64XXX_I2C_BAUD_DIV_M(val)			((val & 0xf) << 3)

#define	MV64XXX_I2C_REG_CONTROL_ACK			0x00000004
#define	MV64XXX_I2C_REG_CONTROL_IFLG			0x00000008
#define	MV64XXX_I2C_REG_CONTROL_STOP			0x00000010
#define	MV64XXX_I2C_REG_CONTROL_START			0x00000020
#define	MV64XXX_I2C_REG_CONTROL_TWSIEN			0x00000040
#define	MV64XXX_I2C_REG_CONTROL_INTEN			0x00000080

/* Ctlr status values */
#define	MV64XXX_I2C_STATUS_BUS_ERR			0x00
#define	MV64XXX_I2C_STATUS_MAST_START			0x08
#define	MV64XXX_I2C_STATUS_MAST_REPEAT_START		0x10
#define	MV64XXX_I2C_STATUS_MAST_WR_ADDR_ACK		0x18
#define	MV64XXX_I2C_STATUS_MAST_WR_ADDR_NO_ACK		0x20
#define	MV64XXX_I2C_STATUS_MAST_WR_ACK			0x28
#define	MV64XXX_I2C_STATUS_MAST_WR_NO_ACK		0x30
#define	MV64XXX_I2C_STATUS_MAST_LOST_ARB		0x38
#define	MV64XXX_I2C_STATUS_MAST_RD_ADDR_ACK		0x40
#define	MV64XXX_I2C_STATUS_MAST_RD_ADDR_NO_ACK		0x48
#define	MV64XXX_I2C_STATUS_MAST_RD_DATA_ACK		0x50
#define	MV64XXX_I2C_STATUS_MAST_RD_DATA_NO_ACK		0x58
#define	MV64XXX_I2C_STATUS_MAST_WR_ADDR_2_ACK		0xd0
#define	MV64XXX_I2C_STATUS_MAST_WR_ADDR_2_NO_ACK	0xd8
#define	MV64XXX_I2C_STATUS_MAST_RD_ADDR_2_ACK		0xe0
#define	MV64XXX_I2C_STATUS_MAST_RD_ADDR_2_NO_ACK	0xe8
#define	MV64XXX_I2C_STATUS_NO_STATUS			0xf8

/* Register defines (I2C bridge) */
#define	MV64XXX_I2C_REG_TX_DATA_LO			0xc0
#define	MV64XXX_I2C_REG_TX_DATA_HI			0xc4
#define	MV64XXX_I2C_REG_RX_DATA_LO			0xc8
#define	MV64XXX_I2C_REG_RX_DATA_HI			0xcc
#define	MV64XXX_I2C_REG_BRIDGE_CONTROL			0xd0
#define	MV64XXX_I2C_REG_BRIDGE_STATUS			0xd4
#define	MV64XXX_I2C_REG_BRIDGE_INTR_CAUSE		0xd8
#define	MV64XXX_I2C_REG_BRIDGE_INTR_MASK		0xdC
#define	MV64XXX_I2C_REG_BRIDGE_TIMING			0xe0

/* Bridge Control values */
#define	MV64XXX_I2C_BRIDGE_CONTROL_WR			0x00000001
#define	MV64XXX_I2C_BRIDGE_CONTROL_RD			0x00000002
#define	MV64XXX_I2C_BRIDGE_CONTROL_ADDR_SHIFT		2
#define	MV64XXX_I2C_BRIDGE_CONTROL_ADDR_EXT		0x00001000
#define	MV64XXX_I2C_BRIDGE_CONTROL_TX_SIZE_SHIFT	13
#define	MV64XXX_I2C_BRIDGE_CONTROL_RX_SIZE_SHIFT	16
#define	MV64XXX_I2C_BRIDGE_CONTROL_ENABLE		0x00080000

/* Bridge Status values */
#define	MV64XXX_I2C_BRIDGE_STATUS_ERROR			0x00000001
#define	MV64XXX_I2C_STATUS_OFFLOAD_ERROR		0xf0000001
#define	MV64XXX_I2C_STATUS_OFFLOAD_OK			0xf0000000


/* Driver states */
enum {
	MV64XXX_I2C_STATE_INVALID,
	MV64XXX_I2C_STATE_IDLE,
	MV64XXX_I2C_STATE_WAITING_FOR_START_COND,
	MV64XXX_I2C_STATE_WAITING_FOR_RESTART,
	MV64XXX_I2C_STATE_WAITING_FOR_ADDR_1_ACK,
	MV64XXX_I2C_STATE_WAITING_FOR_ADDR_2_ACK,
	MV64XXX_I2C_STATE_WAITING_FOR_SLAVE_ACK,
	MV64XXX_I2C_STATE_WAITING_FOR_SLAVE_DATA,
};

/* Driver actions */
enum {
	MV64XXX_I2C_ACTION_INVALID,
	MV64XXX_I2C_ACTION_CONTINUE,
	MV64XXX_I2C_ACTION_OFFLOAD_SEND_START,
	MV64XXX_I2C_ACTION_SEND_START,
	MV64XXX_I2C_ACTION_SEND_RESTART,
	MV64XXX_I2C_ACTION_OFFLOAD_RESTART,
	MV64XXX_I2C_ACTION_SEND_ADDR_1,
	MV64XXX_I2C_ACTION_SEND_ADDR_2,
	MV64XXX_I2C_ACTION_SEND_DATA,
	MV64XXX_I2C_ACTION_RCV_DATA,
	MV64XXX_I2C_ACTION_RCV_DATA_STOP,
	MV64XXX_I2C_ACTION_SEND_STOP,
	MV64XXX_I2C_ACTION_OFFLOAD_SEND_STOP,
};

struct mv64xxx_i2c_regs {
	u8	addr;
	u8	ext_addr;
	u8	data;
	u8	control;
	u8	status;
	u8	clock;
	u8	soft_reset;
};

struct mv64xxx_i2c_data {
	struct i2c_msg		*msgs;
	int			num_msgs;
	int			irq;
	u32			state;
	u32			action;
	u32			aborting;
	u32			cntl_bits;
	void __iomem		*reg_base;
	struct mv64xxx_i2c_regs	reg_offsets;
	u32			addr1;
	u32			addr2;
	u32			bytes_left;
	u32			byte_posn;
	u32			send_stop;
	u32			block;
	int			rc;
	u32			freq_m;
	u32			freq_n;
#if defined(CONFIG_HAVE_CLK)
	struct clk              *clk;
#endif
	wait_queue_head_t	waitq;
	spinlock_t		lock;
	struct i2c_msg		*msg;
	struct i2c_adapter	adapter;
	bool			offload_enabled;
/* 5us delay in order to avoid repeated start timing violation */
	bool			errata_delay;
};

static struct mv64xxx_i2c_regs mv64xxx_i2c_regs_mv64xxx = {
	.addr		= 0x00,
	.ext_addr	= 0x10,
	.data		= 0x04,
	.control	= 0x08,
	.status		= 0x0c,
	.clock		= 0x0c,
	.soft_reset	= 0x1c,
};

static struct mv64xxx_i2c_regs mv64xxx_i2c_regs_sun4i = {
	.addr		= 0x00,
	.ext_addr	= 0x04,
	.data		= 0x08,
	.control	= 0x0c,
	.status		= 0x10,
	.clock		= 0x14,
	.soft_reset	= 0x18,
};

static void
mv64xxx_i2c_prepare_for_io(struct mv64xxx_i2c_data *drv_data,
	struct i2c_msg *msg)
{
	u32	dir = 0;

	drv_data->msg = msg;
	drv_data->byte_posn = 0;
	drv_data->bytes_left = msg->len;
	drv_data->aborting = 0;
	drv_data->rc = 0;
	drv_data->cntl_bits = MV64XXX_I2C_REG_CONTROL_ACK |
		MV64XXX_I2C_REG_CONTROL_INTEN | MV64XXX_I2C_REG_CONTROL_TWSIEN;

	if (msg->flags & I2C_M_RD)
		dir = 1;

	if (msg->flags & I2C_M_TEN) {
		drv_data->addr1 = 0xf0 | (((u32)msg->addr & 0x300) >> 7) | dir;
		drv_data->addr2 = (u32)msg->addr & 0xff;
	} else {
		drv_data->addr1 = MV64XXX_I2C_ADDR_ADDR((u32)msg->addr) | dir;
		drv_data->addr2 = 0;
	}
}

static int mv64xxx_i2c_offload_msg(struct mv64xxx_i2c_data *drv_data)
{
	unsigned long data_reg_hi = 0;
	unsigned long data_reg_lo = 0;
	unsigned long ctrl_reg;
	struct i2c_msg *msg = drv_data->msgs;

	drv_data->msg = msg;
	drv_data->byte_posn = 0;
	drv_data->bytes_left = msg->len;
	drv_data->aborting = 0;
	drv_data->rc = 0;
	/* Only regular transactions can be offloaded */
	if ((msg->flags & ~(I2C_M_TEN | I2C_M_RD)) != 0)
		return -EINVAL;

	/* Only 1-8 byte transfers can be offloaded */
	if (msg->len < 1 || msg->len > 8)
		return -EINVAL;

	/* Build transaction */
	ctrl_reg = MV64XXX_I2C_BRIDGE_CONTROL_ENABLE |
		   (msg->addr << MV64XXX_I2C_BRIDGE_CONTROL_ADDR_SHIFT);

	if ((msg->flags & I2C_M_TEN) != 0)
		ctrl_reg |=  MV64XXX_I2C_BRIDGE_CONTROL_ADDR_EXT;

	if ((msg->flags & I2C_M_RD) == 0) {
		u8 local_buf[8] = { 0 };

		memcpy(local_buf, msg->buf, msg->len);
		data_reg_lo = cpu_to_le32(*((u32 *)local_buf));
		data_reg_hi = cpu_to_le32(*((u32 *)(local_buf+4)));

		ctrl_reg |= MV64XXX_I2C_BRIDGE_CONTROL_WR |
		    (msg->len - 1) << MV64XXX_I2C_BRIDGE_CONTROL_TX_SIZE_SHIFT;

		writel(data_reg_lo,
			drv_data->reg_base + MV64XXX_I2C_REG_TX_DATA_LO);
		writel(data_reg_hi,
			drv_data->reg_base + MV64XXX_I2C_REG_TX_DATA_HI);

	} else {
		ctrl_reg |= MV64XXX_I2C_BRIDGE_CONTROL_RD |
		    (msg->len - 1) << MV64XXX_I2C_BRIDGE_CONTROL_RX_SIZE_SHIFT;
	}

	/* Execute transaction */
	writel(ctrl_reg, drv_data->reg_base + MV64XXX_I2C_REG_BRIDGE_CONTROL);

	return 0;
}

static void
mv64xxx_i2c_update_offload_data(struct mv64xxx_i2c_data *drv_data)
{
	struct i2c_msg *msg = drv_data->msg;

	if (msg->flags & I2C_M_RD) {
		u32 data_reg_lo = readl(drv_data->reg_base +
				MV64XXX_I2C_REG_RX_DATA_LO);
		u32 data_reg_hi = readl(drv_data->reg_base +
				MV64XXX_I2C_REG_RX_DATA_HI);
		u8 local_buf[8] = { 0 };

		*((u32 *)local_buf) = le32_to_cpu(data_reg_lo);
		*((u32 *)(local_buf+4)) = le32_to_cpu(data_reg_hi);
		memcpy(msg->buf, local_buf, msg->len);
	}

}
/*
 *****************************************************************************
 *
 *	Finite State Machine & Interrupt Routines
 *
 *****************************************************************************
 */

/* Reset hardware and initialize FSM */
static void
mv64xxx_i2c_hw_init(struct mv64xxx_i2c_data *drv_data)
{
	if (drv_data->offload_enabled) {
		writel(0, drv_data->reg_base + MV64XXX_I2C_REG_BRIDGE_CONTROL);
		writel(0, drv_data->reg_base + MV64XXX_I2C_REG_BRIDGE_TIMING);
		writel(0, drv_data->reg_base +
			MV64XXX_I2C_REG_BRIDGE_INTR_CAUSE);
		writel(0, drv_data->reg_base +
			MV64XXX_I2C_REG_BRIDGE_INTR_MASK);
	}

	writel(0, drv_data->reg_base + drv_data->reg_offsets.soft_reset);
	writel(MV64XXX_I2C_BAUD_DIV_M(drv_data->freq_m) | MV64XXX_I2C_BAUD_DIV_N(drv_data->freq_n),
		drv_data->reg_base + drv_data->reg_offsets.clock);
	writel(0, drv_data->reg_base + drv_data->reg_offsets.addr);
	writel(0, drv_data->reg_base + drv_data->reg_offsets.ext_addr);
	writel(MV64XXX_I2C_REG_CONTROL_TWSIEN | MV64XXX_I2C_REG_CONTROL_STOP,
		drv_data->reg_base + drv_data->reg_offsets.control);
	drv_data->state = MV64XXX_I2C_STATE_IDLE;
}

static void
mv64xxx_i2c_fsm(struct mv64xxx_i2c_data *drv_data, u32 status)
{
	/*
	 * If state is idle, then this is likely the remnants of an old
	 * operation that driver has given up on or the user has killed.
	 * If so, issue the stop condition and go to idle.
	 */
	if (drv_data->state == MV64XXX_I2C_STATE_IDLE) {
		drv_data->action = MV64XXX_I2C_ACTION_SEND_STOP;
		return;
	}

	/* The status from the ctlr [mostly] tells us what to do next */
	switch (status) {
	/* Start condition interrupt */
	case MV64XXX_I2C_STATUS_MAST_START: /* 0x08 */
	case MV64XXX_I2C_STATUS_MAST_REPEAT_START: /* 0x10 */
		drv_data->action = MV64XXX_I2C_ACTION_SEND_ADDR_1;
		drv_data->state = MV64XXX_I2C_STATE_WAITING_FOR_ADDR_1_ACK;
		break;

	/* Performing a write */
	case MV64XXX_I2C_STATUS_MAST_WR_ADDR_ACK: /* 0x18 */
		if (drv_data->msg->flags & I2C_M_TEN) {
			drv_data->action = MV64XXX_I2C_ACTION_SEND_ADDR_2;
			drv_data->state =
				MV64XXX_I2C_STATE_WAITING_FOR_ADDR_2_ACK;
			break;
		}
		/* FALLTHRU */
	case MV64XXX_I2C_STATUS_MAST_WR_ADDR_2_ACK: /* 0xd0 */
	case MV64XXX_I2C_STATUS_MAST_WR_ACK: /* 0x28 */
		if ((drv_data->bytes_left == 0)
				|| (drv_data->aborting
					&& (drv_data->byte_posn != 0))) {
			if (drv_data->send_stop || drv_data->aborting) {
				drv_data->action = MV64XXX_I2C_ACTION_SEND_STOP;
				drv_data->state = MV64XXX_I2C_STATE_IDLE;
			} else {
				drv_data->action =
					MV64XXX_I2C_ACTION_SEND_RESTART;
				drv_data->state =
					MV64XXX_I2C_STATE_WAITING_FOR_RESTART;
			}
		} else {
			drv_data->action = MV64XXX_I2C_ACTION_SEND_DATA;
			drv_data->state =
				MV64XXX_I2C_STATE_WAITING_FOR_SLAVE_ACK;
			drv_data->bytes_left--;
		}
		break;

	/* Performing a read */
	case MV64XXX_I2C_STATUS_MAST_RD_ADDR_ACK: /* 40 */
		if (drv_data->msg->flags & I2C_M_TEN) {
			drv_data->action = MV64XXX_I2C_ACTION_SEND_ADDR_2;
			drv_data->state =
				MV64XXX_I2C_STATE_WAITING_FOR_ADDR_2_ACK;
			break;
		}
		/* FALLTHRU */
	case MV64XXX_I2C_STATUS_MAST_RD_ADDR_2_ACK: /* 0xe0 */
		if (drv_data->bytes_left == 0) {
			drv_data->action = MV64XXX_I2C_ACTION_SEND_STOP;
			drv_data->state = MV64XXX_I2C_STATE_IDLE;
			break;
		}
		/* FALLTHRU */
	case MV64XXX_I2C_STATUS_MAST_RD_DATA_ACK: /* 0x50 */
		if (status != MV64XXX_I2C_STATUS_MAST_RD_DATA_ACK)
			drv_data->action = MV64XXX_I2C_ACTION_CONTINUE;
		else {
			drv_data->action = MV64XXX_I2C_ACTION_RCV_DATA;
			drv_data->bytes_left--;
		}
		drv_data->state = MV64XXX_I2C_STATE_WAITING_FOR_SLAVE_DATA;

		if ((drv_data->bytes_left == 1) || drv_data->aborting)
			drv_data->cntl_bits &= ~MV64XXX_I2C_REG_CONTROL_ACK;
		break;

	case MV64XXX_I2C_STATUS_MAST_RD_DATA_NO_ACK: /* 0x58 */
		drv_data->action = MV64XXX_I2C_ACTION_RCV_DATA_STOP;
		drv_data->state = MV64XXX_I2C_STATE_IDLE;
		break;

	case MV64XXX_I2C_STATUS_MAST_WR_ADDR_NO_ACK: /* 0x20 */
	case MV64XXX_I2C_STATUS_MAST_WR_NO_ACK: /* 30 */
	case MV64XXX_I2C_STATUS_MAST_RD_ADDR_NO_ACK: /* 48 */
		/* Doesn't seem to be a device at other end */
		drv_data->action = MV64XXX_I2C_ACTION_SEND_STOP;
		drv_data->state = MV64XXX_I2C_STATE_IDLE;
		drv_data->rc = -ENXIO;
		break;

	case MV64XXX_I2C_STATUS_OFFLOAD_OK:
		if (drv_data->send_stop || drv_data->aborting) {
			drv_data->action = MV64XXX_I2C_ACTION_OFFLOAD_SEND_STOP;
			drv_data->state = MV64XXX_I2C_STATE_IDLE;
		} else {
			drv_data->action = MV64XXX_I2C_ACTION_OFFLOAD_RESTART;
			drv_data->state = MV64XXX_I2C_STATE_WAITING_FOR_RESTART;
		}
		break;

	default:
		dev_err(&drv_data->adapter.dev,
			"mv64xxx_i2c_fsm: Ctlr Error -- state: 0x%x, "
			"status: 0x%x, addr: 0x%x, flags: 0x%x\n",
			 drv_data->state, status, drv_data->msg->addr,
			 drv_data->msg->flags);
		drv_data->action = MV64XXX_I2C_ACTION_SEND_STOP;
		mv64xxx_i2c_hw_init(drv_data);
		drv_data->rc = -EIO;
	}
}

static void
mv64xxx_i2c_do_action(struct mv64xxx_i2c_data *drv_data)
{
	switch(drv_data->action) {
	case MV64XXX_I2C_ACTION_OFFLOAD_RESTART:
		mv64xxx_i2c_update_offload_data(drv_data);
		writel(0, drv_data->reg_base +	MV64XXX_I2C_REG_BRIDGE_CONTROL);
		writel(0, drv_data->reg_base +
			MV64XXX_I2C_REG_BRIDGE_INTR_CAUSE);
		/* FALLTHRU */
	case MV64XXX_I2C_ACTION_SEND_RESTART:
		/* We should only get here if we have further messages */
		BUG_ON(drv_data->num_msgs == 0);

		drv_data->msgs++;
		drv_data->num_msgs--;
		if (!(drv_data->offload_enabled &&
				mv64xxx_i2c_offload_msg(drv_data))) {
			drv_data->cntl_bits |= MV64XXX_I2C_REG_CONTROL_START;
			writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);

			/* Setup for the next message */
			mv64xxx_i2c_prepare_for_io(drv_data, drv_data->msgs);
		}
		if (drv_data->errata_delay)
			udelay(5);

		/*
		 * We're never at the start of the message here, and by this
		 * time it's already too late to do any protocol mangling.
		 * Thankfully, do not advertise support for that feature.
		 */
		drv_data->send_stop = drv_data->num_msgs == 1;
		break;

	case MV64XXX_I2C_ACTION_CONTINUE:
		writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_OFFLOAD_SEND_START:
		if (!mv64xxx_i2c_offload_msg(drv_data))
			break;
		else
			drv_data->action = MV64XXX_I2C_ACTION_SEND_START;
		/* FALLTHRU */
	case MV64XXX_I2C_ACTION_SEND_START:
		writel(drv_data->cntl_bits | MV64XXX_I2C_REG_CONTROL_START,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_SEND_ADDR_1:
		writel(drv_data->addr1,
			drv_data->reg_base + drv_data->reg_offsets.data);
		writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_SEND_ADDR_2:
		writel(drv_data->addr2,
			drv_data->reg_base + drv_data->reg_offsets.data);
		writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_SEND_DATA:
		writel(drv_data->msg->buf[drv_data->byte_posn++],
			drv_data->reg_base + drv_data->reg_offsets.data);
		writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_RCV_DATA:
		drv_data->msg->buf[drv_data->byte_posn++] =
			readl(drv_data->reg_base + drv_data->reg_offsets.data);
		writel(drv_data->cntl_bits,
			drv_data->reg_base + drv_data->reg_offsets.control);
		break;

	case MV64XXX_I2C_ACTION_RCV_DATA_STOP:
		drv_data->msg->buf[drv_data->byte_posn++] =
			readl(drv_data->reg_base + drv_data->reg_offsets.data);
		drv_data->cntl_bits &= ~MV64XXX_I2C_REG_CONTROL_INTEN;
		writel(drv_data->cntl_bits | MV64XXX_I2C_REG_CONTROL_STOP,
			drv_data->reg_base + drv_data->reg_offsets.control);
		drv_data->block = 0;
		if (drv_data->errata_delay)
			udelay(5);

		wake_up(&drv_data->waitq);
		break;

	case MV64XXX_I2C_ACTION_INVALID:
	default:
		dev_err(&drv_data->adapter.dev,
			"mv64xxx_i2c_do_action: Invalid action: %d\n",
			drv_data->action);
		drv_data->rc = -EIO;

		/* FALLTHRU */
	case MV64XXX_I2C_ACTION_SEND_STOP:
		drv_data->cntl_bits &= ~MV64XXX_I2C_REG_CONTROL_INTEN;
		writel(drv_data->cntl_bits | MV64XXX_I2C_REG_CONTROL_STOP,
			drv_data->reg_base + drv_data->reg_offsets.control);
		drv_data->block = 0;
		wake_up(&drv_data->waitq);
		break;

	case MV64XXX_I2C_ACTION_OFFLOAD_SEND_STOP:
		mv64xxx_i2c_update_offload_data(drv_data);
		writel(0, drv_data->reg_base +	MV64XXX_I2C_REG_BRIDGE_CONTROL);
		writel(0, drv_data->reg_base +
			MV64XXX_I2C_REG_BRIDGE_INTR_CAUSE);
		drv_data->block = 0;
		wake_up(&drv_data->waitq);
		break;
	}
}

static irqreturn_t
mv64xxx_i2c_intr(int irq, void *dev_id)
{
	struct mv64xxx_i2c_data	*drv_data = dev_id;
	unsigned long	flags;
	u32		status;
	irqreturn_t	rc = IRQ_NONE;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (drv_data->offload_enabled) {
		while (readl(drv_data->reg_base +
				MV64XXX_I2C_REG_BRIDGE_INTR_CAUSE)) {
			int reg_status = readl(drv_data->reg_base +
					MV64XXX_I2C_REG_BRIDGE_STATUS);
			if (reg_status & MV64XXX_I2C_BRIDGE_STATUS_ERROR)
				status = MV64XXX_I2C_STATUS_OFFLOAD_ERROR;
			else
				status = MV64XXX_I2C_STATUS_OFFLOAD_OK;
			mv64xxx_i2c_fsm(drv_data, status);
			mv64xxx_i2c_do_action(drv_data);
			rc = IRQ_HANDLED;
		}
	}
	while (readl(drv_data->reg_base + drv_data->reg_offsets.control) &
						MV64XXX_I2C_REG_CONTROL_IFLG) {
		status = readl(drv_data->reg_base + drv_data->reg_offsets.status);
		mv64xxx_i2c_fsm(drv_data, status);
		mv64xxx_i2c_do_action(drv_data);
		rc = IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&drv_data->lock, flags);

	return rc;
}

/*
 *****************************************************************************
 *
 *	I2C Msg Execution Routines
 *
 *****************************************************************************
 */
static void
mv64xxx_i2c_wait_for_completion(struct mv64xxx_i2c_data *drv_data)
{
	long		time_left;
	unsigned long	flags;
	char		abort = 0;

	time_left = wait_event_timeout(drv_data->waitq,
		!drv_data->block, drv_data->adapter.timeout);

	spin_lock_irqsave(&drv_data->lock, flags);
	if (!time_left) { /* Timed out */
		drv_data->rc = -ETIMEDOUT;
		abort = 1;
	} else if (time_left < 0) { /* Interrupted/Error */
		drv_data->rc = time_left; /* errno value */
		abort = 1;
	}

	if (abort && drv_data->block) {
		drv_data->aborting = 1;
		spin_unlock_irqrestore(&drv_data->lock, flags);

		time_left = wait_event_timeout(drv_data->waitq,
			!drv_data->block, drv_data->adapter.timeout);

		if ((time_left <= 0) && drv_data->block) {
			drv_data->state = MV64XXX_I2C_STATE_IDLE;
			dev_err(&drv_data->adapter.dev,
				"mv64xxx: I2C bus locked, block: %d, "
				"time_left: %d\n", drv_data->block,
				(int)time_left);
			mv64xxx_i2c_hw_init(drv_data);
		}
	} else
		spin_unlock_irqrestore(&drv_data->lock, flags);
}

static int
mv64xxx_i2c_execute_msg(struct mv64xxx_i2c_data *drv_data, struct i2c_msg *msg,
				int is_last)
{
	unsigned long	flags;

	spin_lock_irqsave(&drv_data->lock, flags);
	if (drv_data->offload_enabled) {
		drv_data->action = MV64XXX_I2C_ACTION_OFFLOAD_SEND_START;
		drv_data->state = MV64XXX_I2C_STATE_WAITING_FOR_START_COND;
	} else {
		mv64xxx_i2c_prepare_for_io(drv_data, msg);

		drv_data->action = MV64XXX_I2C_ACTION_SEND_START;
		drv_data->state = MV64XXX_I2C_STATE_WAITING_FOR_START_COND;
	}
	drv_data->send_stop = is_last;
	drv_data->block = 1;
	mv64xxx_i2c_do_action(drv_data);
	spin_unlock_irqrestore(&drv_data->lock, flags);

	mv64xxx_i2c_wait_for_completion(drv_data);
	return drv_data->rc;
}

/*
 *****************************************************************************
 *
 *	I2C Core Support Routines (Interface to higher level I2C code)
 *
 *****************************************************************************
 */
static u32
mv64xxx_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_SMBUS_EMUL;
}

static int
mv64xxx_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct mv64xxx_i2c_data *drv_data = i2c_get_adapdata(adap);
	int rc, ret = num;

	BUG_ON(drv_data->msgs != NULL);
	drv_data->msgs = msgs;
	drv_data->num_msgs = num;

	rc = mv64xxx_i2c_execute_msg(drv_data, &msgs[0], num == 1);
	if (rc < 0)
		ret = rc;

	drv_data->num_msgs = 0;
	drv_data->msgs = NULL;

	return ret;
}

static const struct i2c_algorithm mv64xxx_i2c_algo = {
	.master_xfer = mv64xxx_i2c_xfer,
	.functionality = mv64xxx_i2c_functionality,
};

/*
 *****************************************************************************
 *
 *	Driver Interface & Early Init Routines
 *
 *****************************************************************************
 */
static const struct of_device_id mv64xxx_i2c_of_match_table[] = {
	{ .compatible = "allwinner,sun4i-i2c", .data = &mv64xxx_i2c_regs_sun4i},
	{ .compatible = "marvell,mv64xxx-i2c", .data = &mv64xxx_i2c_regs_mv64xxx},
	{ .compatible = "marvell,mv78230-i2c", .data = &mv64xxx_i2c_regs_mv64xxx},
	{}
};
MODULE_DEVICE_TABLE(of, mv64xxx_i2c_of_match_table);

#ifdef CONFIG_OF
#ifdef CONFIG_HAVE_CLK
static int
mv64xxx_calc_freq(const int tclk, const int n, const int m)
{
	return tclk / (10 * (m + 1) * (2 << n));
}

static bool
mv64xxx_find_baud_factors(const u32 req_freq, const u32 tclk, u32 *best_n,
			  u32 *best_m)
{
	int freq, delta, best_delta = INT_MAX;
	int m, n;

	for (n = 0; n <= 7; n++)
		for (m = 0; m <= 15; m++) {
			freq = mv64xxx_calc_freq(tclk, n, m);
			delta = req_freq - freq;
			if (delta >= 0 && delta < best_delta) {
				*best_m = m;
				*best_n = n;
				best_delta = delta;
			}
			if (best_delta == 0)
				return true;
		}
	if (best_delta == INT_MAX)
		return false;
	return true;
}
#endif /* CONFIG_HAVE_CLK */

static int
mv64xxx_of_config(struct mv64xxx_i2c_data *drv_data,
		  struct device *dev)
{
	/* CLK is mandatory when using DT to describe the i2c bus. We
	 * need to know tclk in order to calculate bus clock
	 * factors.
	 */
#if !defined(CONFIG_HAVE_CLK)
	/* Have OF but no CLK */
	return -ENODEV;
#else
	const struct of_device_id *device;
	struct device_node *np = dev->of_node;
	u32 bus_freq, tclk;
	int rc = 0;

	if (IS_ERR(drv_data->clk)) {
		rc = -ENODEV;
		goto out;
	}
	tclk = clk_get_rate(drv_data->clk);

	rc = of_property_read_u32(np, "clock-frequency", &bus_freq);
	if (rc)
		bus_freq = 100000; /* 100kHz by default */

	if (!mv64xxx_find_baud_factors(bus_freq, tclk,
				       &drv_data->freq_n, &drv_data->freq_m)) {
		rc = -EINVAL;
		goto out;
	}
	drv_data->irq = irq_of_parse_and_map(np, 0);

	/* Its not yet defined how timeouts will be specified in device tree.
	 * So hard code the value to 1 second.
	 */
	drv_data->adapter.timeout = HZ;

	device = of_match_device(mv64xxx_i2c_of_match_table, dev);
	if (!device)
		return -ENODEV;

	memcpy(&drv_data->reg_offsets, device->data, sizeof(drv_data->reg_offsets));

	/*
	 * For controllers embedded in new SoCs activate the
	 * Transaction Generator support and the errata fix.
	 */
	if (of_device_is_compatible(np, "marvell,mv78230-i2c")) {
		drv_data->offload_enabled = true;
		drv_data->errata_delay = true;
	}

out:
	return rc;
#endif
}
#else /* CONFIG_OF */
static int
mv64xxx_of_config(struct mv64xxx_i2c_data *drv_data,
		  struct device *dev)
{
	return -ENODEV;
}
#endif /* CONFIG_OF */

static int
mv64xxx_i2c_probe(struct platform_device *pd)
{
	struct mv64xxx_i2c_data		*drv_data;
	struct mv64xxx_i2c_pdata	*pdata = dev_get_platdata(&pd->dev);
	struct resource	*r;
	int	rc;

	if ((!pdata && !pd->dev.of_node))
		return -ENODEV;

	drv_data = devm_kzalloc(&pd->dev, sizeof(struct mv64xxx_i2c_data),
				GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	r = platform_get_resource(pd, IORESOURCE_MEM, 0);
	drv_data->reg_base = devm_ioremap_resource(&pd->dev, r);
	if (IS_ERR(drv_data->reg_base))
		return PTR_ERR(drv_data->reg_base);

	strlcpy(drv_data->adapter.name, MV64XXX_I2C_CTLR_NAME " adapter",
		sizeof(drv_data->adapter.name));

	init_waitqueue_head(&drv_data->waitq);
	spin_lock_init(&drv_data->lock);

#if defined(CONFIG_HAVE_CLK)
	/* Not all platforms have a clk */
	drv_data->clk = devm_clk_get(&pd->dev, NULL);
	if (!IS_ERR(drv_data->clk)) {
		clk_prepare(drv_data->clk);
		clk_enable(drv_data->clk);
	}
#endif
	if (pdata) {
		drv_data->freq_m = pdata->freq_m;
		drv_data->freq_n = pdata->freq_n;
		drv_data->irq = platform_get_irq(pd, 0);
		drv_data->adapter.timeout = msecs_to_jiffies(pdata->timeout);
		drv_data->offload_enabled = false;
		memcpy(&drv_data->reg_offsets, &mv64xxx_i2c_regs_mv64xxx, sizeof(drv_data->reg_offsets));
	} else if (pd->dev.of_node) {
		rc = mv64xxx_of_config(drv_data, &pd->dev);
		if (rc)
			goto exit_clk;
	}
	if (drv_data->irq < 0) {
		rc = -ENXIO;
		goto exit_clk;
	}

	drv_data->adapter.dev.parent = &pd->dev;
	drv_data->adapter.algo = &mv64xxx_i2c_algo;
	drv_data->adapter.owner = THIS_MODULE;
	drv_data->adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	drv_data->adapter.nr = pd->id;
	drv_data->adapter.dev.of_node = pd->dev.of_node;
	platform_set_drvdata(pd, drv_data);
	i2c_set_adapdata(&drv_data->adapter, drv_data);

	mv64xxx_i2c_hw_init(drv_data);

	rc = request_irq(drv_data->irq, mv64xxx_i2c_intr, 0,
			 MV64XXX_I2C_CTLR_NAME, drv_data);
	if (rc) {
		dev_err(&drv_data->adapter.dev,
			"mv64xxx: Can't register intr handler irq%d: %d\n",
			drv_data->irq, rc);
		goto exit_clk;
	} else if ((rc = i2c_add_numbered_adapter(&drv_data->adapter)) != 0) {
		dev_err(&drv_data->adapter.dev,
			"mv64xxx: Can't add i2c adapter, rc: %d\n", -rc);
		goto exit_free_irq;
	}

	return 0;

exit_free_irq:
	free_irq(drv_data->irq, drv_data);
exit_clk:
#if defined(CONFIG_HAVE_CLK)
	/* Not all platforms have a clk */
	if (!IS_ERR(drv_data->clk)) {
		clk_disable(drv_data->clk);
		clk_unprepare(drv_data->clk);
	}
#endif
	return rc;
}

static int
mv64xxx_i2c_remove(struct platform_device *dev)
{
	struct mv64xxx_i2c_data		*drv_data = platform_get_drvdata(dev);

	i2c_del_adapter(&drv_data->adapter);
	free_irq(drv_data->irq, drv_data);
#if defined(CONFIG_HAVE_CLK)
	/* Not all platforms have a clk */
	if (!IS_ERR(drv_data->clk)) {
		clk_disable(drv_data->clk);
		clk_unprepare(drv_data->clk);
	}
#endif

	return 0;
}

static struct platform_driver mv64xxx_i2c_driver = {
	.probe	= mv64xxx_i2c_probe,
	.remove	= mv64xxx_i2c_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= MV64XXX_I2C_CTLR_NAME,
		.of_match_table = mv64xxx_i2c_of_match_table,
	},
};

module_platform_driver(mv64xxx_i2c_driver);

MODULE_AUTHOR("Mark A. Greer <mgreer@mvista.com>");
MODULE_DESCRIPTION("Marvell mv64xxx host bridge i2c ctlr driver");
MODULE_LICENSE("GPL");

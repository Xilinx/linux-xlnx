/*
 * Generic driver functionality header file for logiI2S FPGA IP core
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
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

#ifndef __LOGII2S_H__
#define __LOGII2S_H__

#include <linux/io.h>

/*
 * I2S device HW constants
 */
#define LOGII2S_MAX_INST	8
#define LOGII2S_REG_STRIDE	8
#define LOGII2S_INST_OFFSET	64

/*
 * I2S device register definitions
 */
#define LOGII2S_HW_VERSION_ROFF	(0 * LOGII2S_REG_STRIDE)
#define LOGII2S_INST_INT_ROFF	(1 * LOGII2S_REG_STRIDE)

/*
 * I2S device instance register definitions
 */
#define LOGII2S_PRESCALE_ROFF	(0 * LOGII2S_REG_STRIDE)
#define LOGII2S_CTRL_ROFF	(1 * LOGII2S_REG_STRIDE)
#define LOGII2S_IMR_ROFF	(2 * LOGII2S_REG_STRIDE)
#define LOGII2S_ISR_ROFF	(3 * LOGII2S_REG_STRIDE)
#define LOGII2S_FIFO_ROFF	(4 * LOGII2S_REG_STRIDE)

/*
 * I2S device instance interrupt Register bit masks
 */
#define LOGII2S_INT_FF		(1 << 0) /* RX/TX FIFO Full Mask */
#define LOGII2S_INT_FAF		(1 << 1) /* RX/TX FIFO Almost Full Mask */
#define LOGII2S_INT_FE		(1 << 2) /* RX/TX FIFO Empty Mask */
#define LOGII2S_INT_FAE		(1 << 3) /* RX/TX FIFO Almost Empty Mask */

#define LOGII2S_INT_MASK_ALL	0xFF /* Mask all interrupts */
#define LOGII2S_INT_ACK_ALL	0xFF /* Clear all interrupts */

/*
 * I2S device instance control Register masks
 */
#define LOGII2S_CTRL_ENABLE	(1 << 0)	/* I2S Instance Enable */
#define LOGII2S_CTRL_FIFO_CLR	(1 << 1)	/* FIFO Clear Mask */
#define LOGII2S_CTRL_SWR	(1 << 2)	/* Soft reset Mask */
#define LOGII2S_CTRL_NONE	(1 << 3)	/* Not used Mask */
#define LOGII2S_CTRL_LRSWAP	(1 << 24)	/* Left/right channel swap */
#define LOGII2S_CTRL_CLKMASTER	(1 << 28)	/* clock master */
#define LOGII2S_CTRL_WSMASTER	(1 << 29)	/* word select master */
#define LOGII2S_CTRL_DIR	(1 << 30)	/* direction (0 = RX, 1 = TX) */
#define LOGII2S_CTRL_WS		(1 << 31)	/* current word select */

#define LOGII2S_LEFT_JUSTIFY	(1 << 26)
#define LOGII2S_RIGHT_JUSTIFY	(1 << 27)

#define LOGII2S_CTRL_WS_MASK	0xFFFFF0	/* word select mask */

#define LOGII2S_RX_INSTANCE	0
#define LOGII2S_TX_INSTANCE	1

/*
 * Max FIFO size in words
 */
#define LOGII2S_FIFO_SIZE_MAX	4096

/*
 * logiI2S port parameter structure
 */
struct logii2s_port {
	struct logii2s_data *data;
	void __iomem *base;
	void *private;
	u32 clock_freq;
	u32 fifo_size;
	u32 almost_full;
	u32 almost_empty;
	unsigned int id;
};

#define logii2s_read(base, offset) readl(base + offset)
#define logii2s_write(base, offset, val) writel(val, (base + offset))

void logii2s_port_reset(struct logii2s_port *port);
int logii2s_check_sample_rate(unsigned int sample_rate);
unsigned int logii2s_port_init_clock(struct logii2s_port *port,
				     unsigned int core_clock_freq,
				     unsigned int sample_rate);
unsigned int logii2s_port_direction(struct logii2s_port *port);
u32 logii2s_port_get_version(struct logii2s_port *port);
u32 logii2s_get_device_iur(void __iomem *base);
void logii2s_port_unmask_int(struct logii2s_port *port, u32 bit_mask);
void logii2s_port_mask_int(struct logii2s_port *port, u32 bit_mask);
u32 logii2s_port_get_isr(struct logii2s_port *port);
void logii2s_port_clear_isr(struct logii2s_port *port, u32 bit_mask);
void logii2s_port_enable_xfer(struct logii2s_port *port);
void logii2s_port_disable_xfer(struct logii2s_port *port);
u32 logii2s_port_read_fifo_word(struct logii2s_port *port);
void logii2s_port_write_fifo_word(struct logii2s_port *port, u32 data);
void logii2s_port_read_fifo(struct logii2s_port *port, u32 *data,
			    unsigned int count);
void logii2s_port_write_fifo(struct logii2s_port *port, u32 *data,
			     unsigned int count);
unsigned int logii2s_port_transfer_data(struct logii2s_port *port,
					u32 *data, unsigned int size);

#endif /* __LOGII2S_H__ */

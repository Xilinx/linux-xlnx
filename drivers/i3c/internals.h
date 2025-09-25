/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_INTERNALS_H
#define I3C_INTERNALS_H

#include <linux/i3c/master.h>
#include <linux/io.h>

void i3c_bus_normaluse_lock(struct i3c_bus *bus);
void i3c_bus_normaluse_unlock(struct i3c_bus *bus);

int i3c_dev_setdasa_locked(struct i3c_dev_desc *dev);
int i3c_dev_do_priv_xfers_locked(struct i3c_dev_desc *dev,
				 struct i3c_priv_xfer *xfers,
				 int nxfers);
int i3c_dev_disable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_enable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_request_ibi_locked(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req);
void i3c_dev_free_ibi_locked(struct i3c_dev_desc *dev);

enum i3c_fifo_endian {
	I3C_FIFO_LITTLE_ENDIAN,
	I3C_FIFO_BIG_ENDIAN,
};

/**
 * i3c_writel_fifo - Write data buffer to 32bit FIFO
 * @addr: FIFO Address to write to
 * @buf: Pointer to the data bytes to write
 * @nbytes: Number of bytes to write
 * @endian: Endianness of FIFO write
 */
static inline void i3c_writel_fifo(void __iomem *addr, const void *buf,
				   int nbytes, enum i3c_fifo_endian endian)
{
	if (endian)
		writesl_be(addr, buf, nbytes / 4);
	else
		writesl(addr, buf, nbytes / 4);

	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, buf + (nbytes & ~3), nbytes & 3);

		if (endian)
			writel_be(tmp, addr);
		else
			writel(tmp, addr);
	}
}

/**
 * i3c_readl_fifo - Read data buffer from 32bit FIFO
 * @addr: FIFO Address to read from
 * @buf: Pointer to the buffer to store read bytes
 * @nbytes: Number of bytes to read
 * @endian: Endianness of FIFO read
 */
static inline void i3c_readl_fifo(const void __iomem *addr, void *buf,
				  int nbytes, enum i3c_fifo_endian endian)
{
	if (endian)
		readsl_be(addr, buf, nbytes / 4);
	else
		readsl(addr, buf, nbytes / 4);

	if (nbytes & 3) {
		u32 tmp;

		if (endian)
			tmp = readl_be(addr);
		else
			tmp = readl(addr);

		memcpy(buf + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

#endif /* I3C_INTERNAL_H */

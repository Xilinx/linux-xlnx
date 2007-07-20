/*
 * xspi_ioctl.h
 *
 * ioctl numbers for Xilinx SPI driver
 *
 * Author: MontaVista Software, Inc.
 *         akonovalov@ru.mvista.com, or source@mvista.com
 *
 * 2004 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifndef _XSPI_IOCTL_H
#define _XSPI_IOCTL_H

#include <linux/ioctl.h>

/* All the SPI options including the readonly ones (labeled RO) */
struct xspi_ioc_options {
	unsigned int has_fifo:1;	/* RO: 1 == has FIFO, 0 == no FIFO */
	unsigned int clk_level:1;	/* RW: 0 == SCK idles low */
	unsigned int clk_phase:1;	/* RW: 0 == data is valid on the 1st SCK edge */
	unsigned int loopback:1;	/* RW: 0 == loopback is OFF */
	unsigned int slave_selects:8;	/* RO: the number of slave selects */
};

struct xspi_ioc_transfer_data {
	int slave_index;
	const char *write_buf;
	char *read_buf;
	int count;
};

#define XSPI_IOC_MAGIC	0xAA

#define XSPI_IOC_MINNR	0xF0
#define XSPI_IOC_MAXNR	0xF4

#define XSPI_IOC_GETOPTS	_IOR(XSPI_IOC_MAGIC, 0xF0, struct xspi_ioc_options)
#define XSPI_IOC_SETOPTS	_IOW(XSPI_IOC_MAGIC, 0xF1, struct xspi_ioc_options)
#define XSPI_IOC_GETSLAVESELECT	_IOR(XSPI_IOC_MAGIC, 0xF2, int)
#define XSPI_IOC_SETSLAVESELECT _IOW(XSPI_IOC_MAGIC, 0xF3, int)
#define XSPI_IOC_TRANSFER	_IOWR(XSPI_IOC_MAGIC, 0xF4, struct xspi_ioc_transfer_data)

#endif /* #ifndef _XSPI_IOCTL_H */

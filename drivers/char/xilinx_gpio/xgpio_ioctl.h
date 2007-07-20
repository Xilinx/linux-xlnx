/*
 * xgpio_ioctl.h
 *
 * ioctl numbers and data structure for Xilinx GPIO driver.
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2005 (c)MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copied from ibm_ocp_gpio.h written by
 *
 *  Armin Kuster akuster@pacbell.net
 *  Sept, 2001
 *
 *  Orignial driver
 *  Author: MontaVista Software, Inc.  <source@mvista.com>
 *          Frank Rowand <frank_rowand@mvista.com>
 *
 * Copyright 2000 MontaVista Software Inc.
 */

#ifndef __XGPIO_IOCTL_H
#define __XGPIO_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define XGPIO_IOCTL_BASE	'Z'

struct xgpio_ioctl_data {
	__u32 chan;
	__u32 mask;
	__u32 data;
};

#define XGPIO_MINOR             185
#define XGPIO_IN		_IOWR(XGPIO_IOCTL_BASE, 0, struct xgpio_ioctl_data)
#define XGPIO_OUT		_IOW (XGPIO_IOCTL_BASE, 1, struct xgpio_ioctl_data)
#define XGPIO_OPEN_DRAIN	_IOW (XGPIO_IOCTL_BASE, 2, struct xgpio_ioctl_data)
#define XGPIO_TRISTATE		_IOW (XGPIO_IOCTL_BASE, 3, struct xgpio_ioctl_data)

#endif /* __XGPIO_IOCTL_H */

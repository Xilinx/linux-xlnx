/*
 *  Copyright (C) 2011 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <asm/hardware/gic.h>
#include "common.h"

#define USB_RST_GPIO	7

extern struct sys_timer xttcpss_sys_timer;

static void __init board_zc770_init(void)
{

	/* initialize the xilinx common code before the board
	 * specific
	 */
	xilinx_init_machine();

	/* Reset USB by toggling MIO7.
	 * Only XM010 (DC1) daughter card resets USB this way,
	 * the other daughter cards use MIO7 for other things.
	 */
	if (of_machine_is_compatible("xlnx,zynq-zc770-xm010")) {
		if (gpio_request(USB_RST_GPIO, "USB Reset"))
			printk(KERN_ERR "ERROR requesting GPIO, USB not reset!");

		if (gpio_direction_output(USB_RST_GPIO, 1))
			printk(KERN_ERR "ERROR setting GPIO direction, USB not reset!");

		gpio_set_value(USB_RST_GPIO, 1);
		gpio_set_value(USB_RST_GPIO, 0);
		gpio_set_value(USB_RST_GPIO, 1);
	}
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-zc770",
	"xlnx,zynq-zc770-xm010",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.handle_irq	= gic_handle_irq,
	.init_machine	= board_zc770_init,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
	.reserve	= xilinx_memory_init,
MACHINE_END

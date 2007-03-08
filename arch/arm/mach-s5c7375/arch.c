/*
 *  linux/arch/arm/mach-s5c7375/arch.c
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS Co.,Ltd.
 *			      Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

extern void s5c7375_time_init(void);
extern unsigned long s5c7375_gettimeoffset(void);

extern void __init s5c7375_init_irq(void);

extern struct sys_timer s5c7375_timer;

MACHINE_START(S5C7375, "S5C7375, SAMSUNG ELECTRONICS Co., Ltd.")
	MAINTAINER("Hyok S. Choi <hyok.choi@samsung.com>")
	INITIRQ(s5c7375_init_irq)
	.timer = &s5c7375_timer,
MACHINE_END

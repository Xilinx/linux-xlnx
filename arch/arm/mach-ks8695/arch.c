/*
 *  linux/arch/arm/mach-ks8695/arch.c
 *
 *  Copyright (C) 2002 Micrel Inc.
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
#include <linux/init.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/arch/ks8695-regs.h>

extern void ks8695_map_io(void);
extern void ks8695_init_irq(void);
extern struct sys_timer ks8695_timer;

#ifdef CONFIG_MACH_KS8695
MACHINE_START(KS8695, "Micrel-KS8695")
	/* Micrel Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_DSM320
MACHINE_START(DSM320, "DLink-DSM320")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT) >> 18) & 0xfffc,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_LITE300
MACHINE_START(KS8695, "Secure Computing SG300")
	/* Secure Computing Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SE4200
MACHINE_START(KS8695, "Secure Computing SE4200")
	/* Secure Computing Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CM4002
MACHINE_START(KS8695, "OpenGear/CM4002")
	/* OpenGear Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CM4008
MACHINE_START(KS8695, "OpenGear/CM4008")
	/* OpenGear Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CM41xx
MACHINE_START(KS8695, "OpenGear/CM41xx")
	/* OpenGear Inc. */
	.phys_io	= KS8695_IO_BASE,
	.io_pg_offst	= ((KS8695_IO_VIRT >> 18) & 0xfffc),
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.timer		= &ks8695_timer,
	.boot_params	= 0x100,
MACHINE_END
#endif


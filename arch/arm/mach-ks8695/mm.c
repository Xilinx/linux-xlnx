/*
 *  linux/arch/arm/mach-ks8695/mm.c
 *
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/sizes.h>
#include <asm/mach/map.h>

/*
 * The only fixed mapping we setup is for the internal register block.
 * This contains the all the device peripheral registers.
 *
 * Logical      Physical        Comment
 * -----------------------------------------
 * FF000000	03FF0000	IO registers
 */
static struct map_desc ks8695_io_desc[] __initdata = {
	{
		.virtual	= KS8695_IO_VIRT,
		.pfn		= __phys_to_pfn(KS8695_IO_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE
	},
};

void __init ks8695_map_io(void)
{
	iotable_init(ks8695_io_desc, ARRAY_SIZE(ks8695_io_desc));
}


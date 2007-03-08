/*
 *  linux/arch/arm/mach-p2001/arch.c
 *
 *  Copyright (C) 2004-2005 Tobias Lorenz
 *
 *  uClinux kernel startup code for p2001
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
#include <asm/sizes.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

extern void __init p2001_init_irq(void);
extern struct sys_timer p2001_timer;

#ifdef CONFIG_P2001_AUTO_DETECT_SDRAM
/* automatic memory detection (by write tests at each memory banks) */
static void __init p2001_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi)
{
	volatile char *mem_start;	// bank 0
	volatile char *mem_end;		// bank n
	char mem_wrap;			// value of bank 0
	char mem_err;			// value of bank n

	/* save memstart */
	mem_start = (char *) CONFIG_DRAM_BASE;
	mem_wrap = *mem_start;

	/* search wrap or error position */
	for(mem_end = mem_start + SZ_1M; mem_end < (char *) (CONFIG_DRAM_BASE + CONFIG_DRAM_SIZE); mem_end += SZ_1M) {
		mem_err = *mem_end;
		(*mem_end)++;
		if (mem_err+1 != *mem_end)
			break;
		if (mem_wrap != *mem_start)
			break;
		*mem_end = mem_err;
	}
	*mem_end = mem_err;

	/* print message */
	printk("Auto detected SDRAM: 0x%08x - 0x%08x (size: %dMB)\n",
		(unsigned int) mem_start, (unsigned int) mem_end, 
		 ((unsigned int) mem_end - (unsigned int) mem_start) / SZ_1M);

	/* give values to mm */
	mi->nr_banks      = 1;
	mi->bank[0].start = (unsigned int) mem_start;
	mi->bank[0].size  = (unsigned int) mem_end - (unsigned int) mem_start;
	mi->bank[0].node  = 0;
}
#endif

MACHINE_START(P2001, "P2001")
	/* Maintainer: Tobias Lorenz <tobias.lorenz@gmx.net> */
#ifdef CONFIG_P2001_AUTO_DETECT_SDRAM
	.fixup = p2001_fixup,
#endif
	.init_irq = p2001_init_irq,
	.timer = &p2001_timer,
MACHINE_END

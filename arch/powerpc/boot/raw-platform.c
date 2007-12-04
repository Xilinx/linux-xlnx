/*
 * The "raw" platform -- for booting from a complete dtb without
 * any fixups.
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "types.h"
#include "io.h"

BSS_STACK(4096);

/* These are labels in the device tree. */
extern u32 memsize[2], timebase, mem_size_cells;

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	u64 memsize64 = memsize[0];
	static const unsigned long line_size = 32;
	static const unsigned long congruence_classes = 256;
	unsigned long addr;
	unsigned long dccr;

	/*
	 * Invalidate the data cache if the data cache is turned off.
	 * - The 405 core does not invalidate the data cache on power-up
	 *   or reset but does turn off the data cache. We cannot assume
	 *   that the cache contents are valid.
	 * - If the data cache is turned on this must have been done by
	 *   a bootloader and we assume that the cache contents are
	 *   valid.
	 */
	__asm__("mfdccr %0": "=r" (dccr));
	if (dccr == 0) {
		for (addr = 0;
		     addr < (congruence_classes * line_size);
		     addr += line_size) {
			__asm__("dccci 0,%0": :"b"(addr));
		}
	}

	if (mem_size_cells == 2) {
		memsize64 <<= 32;
		memsize64 |= memsize[1];
	}

	if (sizeof(void *) == 4 && memsize64 >= 0x100000000ULL)
		memsize64 = 0xffffffff;

	disable_irq();
	timebase_period_ns = 1000000000 / timebase;
	simple_alloc_init(_end, memsize64 - (unsigned long)_end, 32, 64);
	ft_init(_dtb_start, _dtb_end - _dtb_start, 32);
	serial_console_init();
}

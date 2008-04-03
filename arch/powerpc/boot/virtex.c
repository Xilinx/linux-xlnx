/*
 * Old U-boot compatibility for Walnut
 *
 * Author: Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Copyright 2007 IBM Corporation
 *   Based on cuboot-83xx.c, which is:
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "dcr.h"
#include "4xx.h"
#include "io.h"
#include "reg.h"

BSS_STACK(4096);

#include "types.h"
#include "flatdevtree.h"
#include "gunzip_util.h"
#include "../../../include/linux/autoconf.h"

static struct gunzip_state gzstate;

void platform_init(void)
{
	u32 memreg[4];
	u64 start;
	u64 size = 0x2000000;
	int naddr, nsize, i;
	void *root, *memory;
	static const unsigned long line_size = 32;
	static const unsigned long congruence_classes = 256;
	unsigned long addr;
	unsigned long dccr;

	void *dtbz_start;
	u32 dtbz_size;
	void *dtb_addr;
	u32 dtb_size;
	struct boot_param_header dtb_header;
	int len;

        if((mfpvr() & 0xfffff000) == 0x20011000) {
            /* PPC errata 213: only for Virtex-4 FX */
            __asm__("mfccr0  0\n\t"
                    "oris    0,0,0x50000000@h\n\t"
                    "mtccr0  0"
                    : : : "0");
        }

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

	disable_irq();

#ifdef CONFIG_COMPRESSED_DEVICE_TREE

        /** FIXME: flatdevicetrees need the initializer allocated,
            libfdt will fix this. */
	dtbz_start = (void *)CONFIG_COMPRESSED_DTB_START;
	dtbz_size = CONFIG_COMPRESSED_DTB_SIZE;
	/** get the device tree */
	gunzip_start(&gzstate, dtbz_start, dtbz_size);
	gunzip_exactly(&gzstate, &dtb_header, sizeof(dtb_header));

	dtb_size = dtb_header.totalsize;
	// printf("Allocating 0x%lx bytes for dtb ...\n\r", dtb_size);

	dtb_addr = _end; // Should be allocated?

	gunzip_start(&gzstate, dtbz_start, dtbz_size);
	len = gunzip_finish(&gzstate, dtb_addr, dtb_size);
	if (len != dtb_size)
		fatal("ran out of data!  only got 0x%x of 0x%lx bytes.\n\r",
				len, dtb_size);
	printf("done 0x%x bytes\n\r", len);
	simple_alloc_init(0x800000, size - (unsigned long)0x800000, 32, 64);
	ft_init(dtb_addr, dtb_size, 32);
#else
        /** FIXME: flatdevicetrees need the initializer allocated,
            libfdt will fix this. */
	simple_alloc_init(_end, size - (unsigned long)_end, 32, 64);
	ft_init(_dtb_start, _dtb_end - _dtb_start, 32);
#endif

	root = finddevice("/");
	if (getprop(root, "#address-cells", &naddr, sizeof(naddr)) < 0)
		naddr = 2;
	if (naddr < 1 || naddr > 2)
		fatal("Can't cope with #address-cells == %d in /\n\r", naddr);

	if (getprop(root, "#size-cells", &nsize, sizeof(nsize)) < 0)
		nsize = 1;
	if (nsize < 1 || nsize > 2)
		fatal("Can't cope with #size-cells == %d in /\n\r", nsize);

	memory = finddevice("/memory@0");
	if (! memory) {
		fatal("Need a memory@0 node!\n\r");
	}
	if (getprop(memory, "reg", memreg, sizeof(memreg)) < 0)
		fatal("Need a memory@0 node!\n\r");

	i = 0;
	start = memreg[i++];
	if(naddr == 2) {
		start = (start << 32) | memreg[i++];
	}
	size = memreg[i++];
	if (nsize == 2)
		size = (size << 32) | memreg[i++];

	// timebase_period_ns = 1000000000 / timebase;
	serial_console_init();
	if (console_ops.open) 
		console_ops.open();

#ifdef CONFIG_COMPRESSED_DEVICE_TREE
	printf("Using compressed device tree at 0x%x\n\r", CONFIG_COMPRESSED_DTB_START);
#else
#endif
        printf("booting virtex\n\r");
        printf("memstart=0x%x\n\r", start);
        printf("memsize=0x%x\n\r", size);
}

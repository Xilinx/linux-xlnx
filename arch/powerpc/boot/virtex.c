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

#define UART_DLL		0	/* Out: Divisor Latch Low */
#define UART_DLM		1	/* Out: Divisor Latch High */
#define UART_FCR		2	/* Out: FIFO Control Register */
#define UART_FCR_CLEAR_RCVR 	0x02 	/* Clear the RCVR FIFO */
#define UART_FCR_CLEAR_XMIT	0x04 	/* Clear the XMIT FIFO */
#define UART_LCR		3	/* Out: Line Control Register */
#define UART_MCR		4	/* Out: Modem Control Register */
#define UART_MCR_RTS		0x02 	/* RTS complement */
#define UART_MCR_DTR		0x01 	/* DTR complement */
#define UART_LCR_DLAB		0x80 	/* Divisor latch access bit */
#define UART_LCR_WLEN8		0x03 	/* Wordlength: 8 bits */

/* This function is only needed when there is no boot loader to 
   initialize the UART
*/
static int virtex_ns16550_console_init(void *devp)
{
	int n;
	unsigned long reg_phys;
	unsigned char *regbase;
	u32 regshift, clk, spd;
	u16 divisor;

	n = getprop(devp, "virtual-reg", &regbase, sizeof(regbase));
	if (n != sizeof(regbase)) {
		if (!dt_xlate_reg(devp, 0, &reg_phys, NULL))
			return -1;

		regbase = (void *)reg_phys + 3;
	}
	regshift = 2;
	
	n = getprop(devp, "current-speed", (void *)&spd, sizeof(spd));
	if (n != sizeof(spd))
		spd = 9600;

	/* should there be a default clock rate?*/
	n = getprop(devp, "clock-frequency", (void *)&clk, sizeof(clk));
	if (n != sizeof(clk))
		return -1;

	divisor = clk / (16 * spd);

	/* Access baud rate */
	out_8(regbase + (UART_LCR << regshift), UART_LCR_DLAB);

	/* Baud rate based on input clock */
	out_8(regbase + (UART_DLL << regshift), divisor & 0xFF);
	out_8(regbase + (UART_DLM << regshift), divisor >> 8);

	/* 8 data, 1 stop, no parity */
	out_8(regbase + (UART_LCR << regshift), UART_LCR_WLEN8);

	/* RTS/DTR */
	out_8(regbase + (UART_MCR << regshift), UART_MCR_RTS | UART_MCR_DTR);

	/* Clear transmitter and receiver */
	out_8(regbase + (UART_FCR << regshift), 
				UART_FCR_CLEAR_XMIT | UART_FCR_CLEAR_RCVR);
	return 0;
}

/* For virtex, the kernel may be loaded without using a bootloader and if so
   some UARTs need more setup than is provided in the normal console init
*/
static int virtex_serial_console_init(void)
{
	void *devp;
	char devtype[MAX_PROP_LEN];
	char path[MAX_PATH_LEN];

	devp = finddevice("/chosen");
	if (devp == NULL)
		return -1;

	if (getprop(devp, "linux,stdout-path", path, MAX_PATH_LEN) > 0) {
		devp = finddevice(path);
		if (devp == NULL)
			return -1;

		if ((getprop(devp, "device_type", devtype, sizeof(devtype)) > 0)
				&& !strcmp(devtype, "serial") 
				&& (dt_is_compatible(devp, "ns16550")))	
				virtex_ns16550_console_init(devp);
	}
	return 0;
}

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

#ifdef CONFIG_COMPRESSED_DEVICE_TREE
	void *dtbz_start;
	u32 dtbz_size;
	void *dtb_addr;
	u32 dtb_size;
	struct boot_param_header dtb_header;
	int len;
#endif

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
	virtex_serial_console_init();
	serial_console_init();
	if (console_ops.open) 
		console_ops.open();

#ifdef CONFIG_COMPRESSED_DEVICE_TREE
	printf("Using compressed device tree at 0x%x\n\r", CONFIG_COMPRESSED_DTB_START);
#else
#endif
        printf("booting virtex\n\r");
        printf("memstart=0x%Lx\n\r", start);
        printf("memsize=0x%Lx\n\r", size);
}

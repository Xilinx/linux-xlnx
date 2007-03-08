/*
 * linux/include/asm-arm/arch-ks8695/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *  (C) Copyright 2006 Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

.macro addruart,rx
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1                 @ MMU enabled?
	moveq	\rx, #0x03000000	@ 0x03ffe000
	orreq	\rx, \rx, #0x00ff0000
	movne	\rx, #0xff000000	@ 0xff00e000
	orr	\rx, \rx, #0x0000e000
.endm

.macro senduart,rd,rx
	str	\rd, [\rx, #0x4]
.endm

.macro waituart,rd,rx
1:	ldr	\rd, [\rx, #0x14]
	and	\rd, \rd, #0x40		@ check TEMT bit
	teq	\rd, #0x40
	bne	1b
.endm

.macro busyuart,rd,rx
.endm


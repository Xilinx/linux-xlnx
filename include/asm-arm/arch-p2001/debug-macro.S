/* linux/include/asm-arm/arch-p2001/debug-macro.S
 *
 * Debugging macro include header
 *
 * by Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

		/* UART base address */
		.macro	addruart,rx
		mov	\rx, #0x00140000
		.endm

		/* send char */
		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		/* wait for end of transmission */
		.macro	busyuart,rd,rx
1001:		ldrb	\rd, [\rx, #0x24]
		cmp	\rd, #0
		bne	1001b
		.endm

		/* wait for fifo space */
		/* quickest way here: test for >0x20 chars in fifo */
		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x24]
		tst	\rd, #0x20
		bne	1001b
		.endm


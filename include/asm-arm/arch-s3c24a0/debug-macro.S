/* linux/include/asm-arm/arch-s3c24a0/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 2005 Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

       		.macro  addruart, rx
                .endm

		.macro	senduart,rd,rx
 		mov	r1, #0x00000002
		MRC	p14, 0, r3, c0, c0 	;read comms control reg
 		AND	r3, r3, r1
		CMP	r3, #2
 		BEQ	$-0x0C
		CMP	r0, #0x0a
		BNE	$+0x1C
		mov	r3, #0x0d
		MCR	p14, 0, r3, c1, c0	;write comms data reg
		MRC	p14, 0, r3, c0, c0 	;read comms control reg
		AND	r3, r3, r1
		CMP	r3, #2
		BEQ	$-0x0C
		MCR	p14, 0, r0, c1, c0	;write comms data reg
		mov 	pc,r14
                .endm 

       		.macro	busyuart, rd, rx
		.endm

		.macro	waituart,rd,rx
		.endm
                


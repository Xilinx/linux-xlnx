/*
 *  linux/include/asm-armnommu/hardware/dcc.h
 *
 *  Copyright (C) 2004 Hyok S. Choi, Samsung Electronics Co.,Ltd.
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
#ifndef __DCC_PUTS__
#define __DCC_PUTS__
static void dcc_puts(const char *p)
{
#ifndef CONFIG_JTAG_DCC_OUTPUT_DISABLE
	/*
		r0 = string	; string address
		r1 = 2		; state check bit (write)
		r4 = *string	; character
	*/
		__asm__ __volatile__(
			"	ldrb r4, [%0]			@ load a char\n"
			"1:	mrc	p14, 0, r3, c0, c0 	@ read comms control reg\n"
			"	and r3, r3, #2			@ the write buffer status\n"
			"	cmp r3, #2			@ is it available?\n"
			"	beq 1b				@ is not, wait till then\n"
			"	mcr p14, 0, r4, c1, c0		@ write it\n"
			"	cmp r4, #0x0a			@ is it LF?\n"
			"	bne 2f				@ if it is not, continue\n"
			"	mov r4, #0x0d			@ set the CR\n"
			"	b   1b				@ loop for writing CR\n"			
			"2:	ldrb r4, [%0, #1]!		@ load a char\n"
			"	cmp r4, #0x0			@ test is null\n"
			"	bne 1b				@ if it is not yet, loop"
			: /* no output register */
			: "r" (p)
			: "r3", "r4");
#endif
}
#endif

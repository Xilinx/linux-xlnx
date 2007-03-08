/*
 * include/asm-arm/arch-ks8695/entry-macro.S
 *
 * Low-level IRQ helper macros for KS8695 based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

.macro	disable_fiq
.endm


.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqnr, =KS8695_IO_VIRT+KS8695_INT_MASK_STATUS
	ldr	\irqstat, [\irqnr]		@ get masked status

	mov	\irqnr, #0
1001:	tst	\irqstat, #1
	bne	1002f
	add	\irqnr, \irqnr, #1
	mov	\irqstat, \irqstat, lsr #1
	cmp	\irqnr, #32
	bcc	1001b
1002:	@ EQ will be set if we reach
.endm


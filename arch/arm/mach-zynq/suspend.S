/*
 * Suspend support for Zynq
 *
 *  Copyright (C) 2012 Xilinx
 *
 *  Soren Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/linkage.h>

#define ARMPLL_CTRL_OFFS	0x100
#define DDRPLL_CTRL_OFFS	0x104
#define PLLSTATUS_OFFS		0x10c
#define DDR_CLK_CTRL_OFFS	0x124
#define DCI_CLK_CTRL_OFFS	0x128
#define MODE_STS_OFFS		0x54

#define PLL_RESET_MASK		1
#define PLL_PWRDWN_MASK		(1 << 1)
#define PLL_BYPASS_MASK		(1 << 4)
#define DCICLK_ENABLE_MASK	1
#define DDRCLK_ENABLE_MASK	3
#define ARM_LOCK_MASK		(1 << 0)
#define DDR_LOCK_MASK		(1 << 1)
#define DDRC_STATUS_MASK	7

#define DDRC_OPMODE_SR		3
#define MAXTRIES		100

	.text

/**
 * zynq_sys_suspend - Enter suspend
 * @ddrc_base:	Base address of the DDRC
 * @slcr_base:	Base address of the SLCR
 * Returns -1 if DRAM subsystem is not gated off, 0 otherwise.
 *
 * This function is moved into OCM and finishes the suspend operation. I.e. DDR
 * related clocks are gated off and the DDR PLL is bypassed.
 */
ENTRY(zynq_sys_suspend)
	push	{r4 - r7}

	/* Check DDRC is in self-refresh mode */
	ldr	r2, [r0, #MODE_STS_OFFS]
	and	r2, #DDRC_STATUS_MASK
	cmp	r2, #DDRC_OPMODE_SR
	movweq	r3, #0xff00
	bne	suspend

	mov	r3, #MAXTRIES
	movw	r4, #0xfff0
	movt	r4, #0x1f
	/* Wait for command queue empty */
1:	subs	r3, #1
	movweq	r3, #0xff00
	beq	suspend
	dsb	sy
	ldr	r2, [r0, #MODE_STS_OFFS]
	ands	r2, r4
	bne	1b

	dsb	sy

	/*
	 * Wait for DDRC pipeline/queues to drain.
	 * We should wait ~40 DDR cycles. DDR is still at full speed while the
	 * CPU might already run in PLL bypass mode. The fastest speed the CPU
	 * runs at is ~1 GHz ~ 2 * DDR speed.
	 */
	mov	r3, #160
1:	nop
	subs	r3, #1
	bne	1b

	dsb	sy

	/* read back CAM status once more */
	ldr	r2, [r0, #MODE_STS_OFFS]
	ands	r2, r4
	movwne	r3, #0xff00
	bne	suspend

	/* Stop DDR clocks */
	ldr	r2, [r1, #DDR_CLK_CTRL_OFFS]
	bic	r2, #DDRCLK_ENABLE_MASK
	str	r2, [r1, #DDR_CLK_CTRL_OFFS]

	dmb	st

	ldr	r2, [r1, #DCI_CLK_CTRL_OFFS]
	bic	r2, #DCICLK_ENABLE_MASK
	str	r2, [r1, #DCI_CLK_CTRL_OFFS]

	dmb	st

	/* Bypass and powerdown DDR PLL */
	ldr	r2, [r1, #DDRPLL_CTRL_OFFS]
	orr	r2, #PLL_BYPASS_MASK
	str	r2, [r1, #DDRPLL_CTRL_OFFS]
	orr	r2, #(PLL_PWRDWN_MASK | PLL_RESET_MASK)
	str	r2, [r1, #DDRPLL_CTRL_OFFS]

	/* Bypass and powerdown ARM PLL */
	ldr	r2, [r1, #ARMPLL_CTRL_OFFS]
	orr	r2, #PLL_BYPASS_MASK
	str	r2, [r1, #ARMPLL_CTRL_OFFS]
	orr	r2, #(PLL_PWRDWN_MASK | PLL_RESET_MASK)
	str	r2, [r1, #ARMPLL_CTRL_OFFS]

suspend:
	dsb	sy
	wfi
	dsb	sy
	cmp	r3, #0xff00
	moveq	r0, #-1
	beq	exit

	/* Power up ARM PLL */
	ldr	r2, [r1, #ARMPLL_CTRL_OFFS]
	bic	r2, #(PLL_PWRDWN_MASK | PLL_RESET_MASK)
	str	r2, [r1, #ARMPLL_CTRL_OFFS]
	/* wait for lock */
1:	ldr	r2, [r1, #PLLSTATUS_OFFS]
	ands	r2, #ARM_LOCK_MASK
	beq	1b

	dsb	sy

	/* Disable ARM PLL bypass */
	ldr	r2, [r1, #ARMPLL_CTRL_OFFS]
	bic	r2, #PLL_BYPASS_MASK
	str	r2, [r1, #ARMPLL_CTRL_OFFS]

	dmb	st

	/* Power up DDR PLL */
	ldr	r2, [r1, #DDRPLL_CTRL_OFFS]
	bic	r2, #(PLL_PWRDWN_MASK | PLL_RESET_MASK)
	str	r2, [r1, #DDRPLL_CTRL_OFFS]
	/* wait for lock */
1:	ldr	r2, [r1, #PLLSTATUS_OFFS]
	ands	r2, #DDR_LOCK_MASK
	beq	1b

	dsb	sy

	/* Disable DDR PLL bypass */
	ldr	r2, [r1, #DDRPLL_CTRL_OFFS]
	bic	r2, #PLL_BYPASS_MASK
	str	r2, [r1, #DDRPLL_CTRL_OFFS]

	dmb	st

	/* Start DDR clocks */
	ldr	r2, [r1, #DCI_CLK_CTRL_OFFS]
	orr	r2, #DCICLK_ENABLE_MASK
	str	r2, [r1, #DCI_CLK_CTRL_OFFS]

	dmb	st

	ldr	r2, [r1, #DDR_CLK_CTRL_OFFS]
	orr	r2, #DDRCLK_ENABLE_MASK
	str	r2, [r1, #DDR_CLK_CTRL_OFFS]

	dsb	sy

	mov	r0, #0
exit:	pop	{r4 - r7}
	bx	lr

ENTRY(zynq_sys_suspend_sz)
	.word	. - zynq_sys_suspend

	ENDPROC(zynq_sys_suspend)

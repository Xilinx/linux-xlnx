/*
 *  linux/arch/armnommu/mach-s5c7375/head.S
 *
 *  Copyright (C) 2003 Hyok S. Choi
 *  <hyok.choi@samsung.com>
 *
 *  uClinux kernel startup code for s5c7375
 *  which has no proper bootloader for linux startup
 *  because of XIP.
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
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/mach/arch.h>

#define MACHINFO_TYPE           0
#define MACHINFO_PHYSRAM        4
#define MACHINFO_PHYSIO         8
#define MACHINFO_PGOFFIO        12
#define MACHINFO_NAME           16


/*
 * Kernel startup entry point.
 */
	__INIT
	.type	stext, #function
ENTRY(stext)
	mov	r12, r0
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE	@ make sure svc mode
	msr	cpsr_c, r0			@ and all irqs disabled

/*
 * ARM920T need MMU enable for D-cache.
 * we just make the identical table before the MMU enabling.
 */
#ifdef CONFIG_CPU_MXU_ENABLE
	/* Write domain id (cp15_r3) */
	mvn	r0, #0			/* Domains 0, 1 = client */
	mcr	p15, 0, r0, c3, c0, 0	/* load domain access register */
	/* Set control register v4 */

	mov	r0, #0
	mcr	p15, 0, r0, c13, c0, 0	/* zero PID */
	mcr	p15, 0, r0, c7, c7, 0	/* invalidate I,D caches */
	mcr	p15, 0, r0, c7, c10, 4	/* drain write buffer */
	mcr	p15, 0, r0, c8, c7, 0	/* invalidate I,D TLBs */
	mrc	p15, 0, r0, c1, c0, 0	/* get control register */
	bic	r0, r0, #0x0001		/* disable MMU */
	mcr	p15, 0, r0, c1, c0, 0	/* write control register */
init_ttb:
        ldr     r0,=0x00004000         @ set start of Translation Table base (16k Boundary)
        mcr     p15, 0, r0, c2, c0, 0      @ write to CP15 register 2
	/* 
	 *
	 * Create translation table for flat mapping
	 * Top 12 bits of VA is pointer into table
	 * Create 4096 entries from 000xxxxx to fffxxxxx
	 *
	 */
	mov		r1, #0x1000
	subs 	r1,r1,#1
        mov     r2,#0b110000000000           @ set access permissions (AP) for full access SVC/USR (11:10)
        orr     r2,r2,#0b000111100000       @ set for domain 15 (8:5)
        orr     r2,r2,#0b000000010000       @ must be 1 (4)
        orr     r2,r2,#0b000000000000       @ set non cachable non bufferable (CB) (3:2)
        orr     r2,r2,#0b000000000010       @ set for 1Mb section (1:0)
init_ttb_1:
        orr     r3,r2,r1,lsl#20             @ use loop counter to create individual table entries
        str     r3,[r0,r1,lsl#2]            @ str r3 at TTB base + loopcount*4
        subs    r1,r1,#1                    @ decrement loop counter
        bpl     init_ttb_1

	/* resetting the SDRAM area to cacheable*/
	mov	r1, #0x003				@ loop counter

	mov     r2,#0b110000000000           @ set access permissions (AP) for full access SVC/USR (11:10)
	orr     r2,r2,#0b000111100000       @ set for domain 15 (8:5)
	orr     r2,r2,#0b000000010000       @ must be 1 (4)
	orr     r2,r2,#0b000000001100       @ set cachable but unbufferable (CB) (3:2)
	orr     r2,r2,#0b000000000010       @ set for 1Mb section (1:0)

init_ttb_2:
        orr     r3,r2,r1,lsl#20             @ use loop counter to create individual table entries
	str     r3,[r0,r1,lsl#2]            @ str r3 at TTB base + loopcount*4
	subs    r1,r1,#1                    @ decrement loop counter
	bpl     init_ttb_2
	@init_domains : we define all domains are manager. so no access permission check is occured.
	mvn     r0, #0				@ trick. 0 -1 == 0xFFFFFFFF
	mcr     p15, 0, r0, c3, c0, 0       @ write to CP15 register 5
/*
 * set global core configurations
 */
        mrc     p15, 0, r0, c1, c0, 0       @ read CP15 register 1 into r0
        
        orr     r0, r0, #(0x1 <<12)         @ enable I Cache
        orr     r0, r0, #(0x1 <<2)          @ enable D Cache
        orr     r0, r0, #(0b11 <<30)        @ enable asynchronous clocking mode
        orr     r0, r0, #0x1                @ enable MMU

        mcr     p15, 0, r0, c1, c0, 0       @ write cp15 register 1

#endif

/*
 * stuffs for cache are done.
 * Now we setup the stack and machine id, and start the kernel!
 */

        adr	r5, LC0
        ldmia	r5, {r5, r6, r8, r9, sp}			@ Setup stack

        /*  Copy data sections to their new home.  */


        /*  Clear BSS */
        mov	r4, #0
1:      cmp	r5, r8					
        strcc	r4, [r5],#4
        bcc	1b

        /*  Pretend we know what our processor code is (for arm_id)   */

	ldr	r2, S5C7375_PROCESSOR_TYPE

	str     r2, [r6]
	ldr     r2, S5C7375_MACH_TYPE
	str     r2, [r9]

        mov fp, #0
        b	start_kernel
        
LC0:	.long	__bss_start
        .long	processor_id
        .long	_end
	.long   __machine_arch_type
        .long	init_thread_union+8192
	
S5C7375_PROCESSOR_TYPE:
	.long	0x41129200
S5C7375_MACH_TYPE:
	.long	MACH_TYPE_S5C7375

#include "../kernel/head-common.S"

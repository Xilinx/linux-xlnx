/*
 *  arch/armnommu/mach-s3c24a0/entry-macro.S
 * 
 *  Heechul Yun <heechul.yun@samsung.com>
 *  Hyok S. Choi <hyok.choi@samsung.com> for fixup patch
 */


#include <asm/hardware.h>        
		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
                mov     \irqstat,   #io_p2v(0x40000000) @ Virtual Address
                add     \irqstat, \irqstat,#0x00200000  @ INT Ctrl base
                                                                                                                                                      
                ldr     \irqnr,   [\irqstat,#0x14]      @ INTOFFSET
                cmp     \irqnr,#0
                beq     1001f
		mov	r0, \irqnr
		bl	fixup_irq
		b	1002f
1001:
                ldr     \irqstat,[\irqstat,#-0x4]       @ INTPND
                tst     \irqstat,#0x1                   @ EINT0_2 happens ?
1002:

                .endm 
		.macro	disable_fiq
		.endm

		/* we don't have an irq priority table */
		.macro irq_prio_table
		.endm


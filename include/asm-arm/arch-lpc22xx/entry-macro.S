/*
 *  include/arm/arch-lpc22xx/entry-macro.S
 *
 *  Copyright (C) 2004 Philips Semiconductors
 *
 *  defines machine dependent entry macros.
 *  included in the arch/armnommu/kernel/entry.S
 *  
 */
 
 #include "irqs.h"
 
.EQU	VICIrqStatus,	0xFFFFF000

#if defined(CONFIG_ARCH_LPC22XX)
        .macro  disable_fiq
        .endm

		.macro get_irqnr_and_base, irqnr, irqstat, base, tmp
		
		ldr	\irqstat, =(VICIrqStatus)	@ load address of interrupt pending
		ldr	\irqstat, [\irqstat]	@ get the register content

		mov     \irqnr, #0
1001:
		tst     \irqstat, #1
		bne     1002f
		add     \irqnr, \irqnr, #1
		mov     \irqstat, \irqstat, lsr #1
		cmp     \irqnr, #NR_IRQS
		bcc     1001b
1002:		/* EQ will be set if we reach 32 */
		.endm

		.macro irq_prio_table
		.endm
#endif

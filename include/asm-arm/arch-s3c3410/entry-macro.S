/*
 *  arch/armnommu/mach-s3c3410/entry-macro.S
 *
 *  Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 *  Samsung Electronics Co.,Ltd.
 *
 *  defines machine dependent entry macros.
 *  included in the arch/armnommu/kernel/entry.S
 *  
 */

#if defined(CONFIG_ARCH_S3C3410)
                .macro  disable_fiq
                .endm

		.macro get_irqnr_and_base, irqnr, irqstat, base, tmp
		
		ldr	\irqstat, =(S3C3410X_INTPND)	@ load address of interrupt pending
@		ldr	\irqstat, [\irqstat]	@ register INTPND
		ldr	\irqnr, [\irqstat]	@ \irqnr = INTPND
		add	\irqstat, \irqstat, #(S3C3410X_INTMSK-S3C3410X_INTPND)
		ldr	\irqstat, [\irqstat]	@ \irqstat = INTMSK
		and	\irqstat, \irqnr, \irqstat @ \irqstat = (INTPND & INTMSK)

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

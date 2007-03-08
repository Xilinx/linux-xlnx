/*
 *  arch/armnommu/mach-s3c44b0x/entry-macro.S
 *
 *  defines machine dependent entry macros.
 *  included in the arch/armnommu/kernel/entry.S
 */

#ifndef CONFIG_ARCH_S3C44B0
#error
#endif
                .macro  disable_fiq
		/* FIXME */
                .endm

		.macro get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr     \base, =S3C44B0X_I_ISPR
		ldr     \base, [\base]
		mov     \irqnr, #0
2222:           
		tst     \base, #1 
		bne     1111f
		add     \irqnr, \irqnr, #1      
		mov     \base, \base, lsr #1
		cmp     \irqnr, #NR_IRQS
		bcc     2222b
1111:           
		.endm

		.macro irq_prio_table
		/* FIXME */
		.endm

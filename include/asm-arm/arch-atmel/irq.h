/*
 *  linux/include/asm-arm/arch-atmel/irq.h:
 * 2001 Erwin Authried
 */

#ifndef __ASM_ARCH_IRQ_H__
#define __ASM_ARCH_IRQ_H__


#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>


#define fixup_irq(x) (x)

extern void at91_mask_irq(unsigned int irq);
extern void at91_unmask_irq(unsigned int irq);
extern void at91_mask_ack_irq(unsigned int irq);

#endif /* __ASM_ARCH_IRQ_H__ */

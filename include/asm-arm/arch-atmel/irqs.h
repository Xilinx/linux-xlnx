/*
 *  linux/include/asm-arm/arch-atmel/irqs.h:
 * 2001 Mindspeed
 */
#ifndef __ASM_ARCH_IRQS_H__
#define __ASM_ARCH_IRQS_H__


#ifdef CONFIG_CPU_AT91X40
/*
 ******************* AT91x40xxx ********************
 */

#define NR_IRQS		24
#define VALID_IRQ(i)	(i<=8 ||(i>=16 && i<NR_IRQS))


#define IRQ_FIQ		0
#define IRQ_SWI		1
#define IRQ_USART0	2
#define IRQ_USART1	3
#define IRQ_TC0		4
#define IRQ_TC1		5
#define IRQ_TC2		6
#define IRQ_WD		7
#define IRQ_PIOA	8

#define IRQ_EXT0	16
#define IRQ_EXT1	17
#define IRQ_EXT2	18

#elif CONFIG_CPU_AT91X63
/*
 ******************* AT91x63xxx ********************
 */

#define NR_IRQS		32
#define VALID_IRQ(i)	(i<=14 ||(i>=28 && i<NR_IRQS))

#define IRQ_FIQ		0
#define IRQ_SWI		1
#define IRQ_USART0	2
#define IRQ_USART1	3
#define IRQ_USART2	4
#define IRQ_SP		5
#define IRQ_TC0		6
#define IRQ_TC1		7
#define IRQ_TC2		8
#define IRQ_TC3		9
#define IRQ_TC4		10
#define IRQ_TC5		11
#define IRQ_WD		12
#define IRQ_PIOA	13
#define IRQ_PIOB	14

#define IRQ_EXT0	31
#define IRQ_EXT1	30
#define IRQ_EXT2	29
#define IRQ_EXT3	28

#else 
  #error "Configuration error: No CPU defined"
#endif

#endif /* __ASM_ARCH_IRQS_H__ */

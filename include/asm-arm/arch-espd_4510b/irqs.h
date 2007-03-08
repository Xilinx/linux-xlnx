/*
 *  linux/include/asm-armnommu/arch-espd_4510b/irqs.h
 *
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
 *
 *  Based on:
 *  linux-2.4.x/asm/arch-samsung/irqs.h:
 *  Mac Wang <mac@os.nctu.edu.tw>
 *
 */
#ifndef __ASM_ARCH_IRQS_H__
#define __ASM_ARCH_IRQS_H__
#define NR_IRQS		21
#define VALID_IRQ(i)	(i<=8 ||(i>=16 && i<NR_IRQS))

#define INT_EXTINT0	0
#define INT_EXTINT1	1
#define INT_EXTINT2	2
#define INT_EXTINT3	3
#define INT_UARTTX0	4
#define INT_UARTRX0	5
#define INT_UARTTX1	6
#define INT_UARTRX1	7
#define INT_GDMA0	8
#define INT_GDMA1	9
#define INT_TIMER0	10
#define INT_TIMER1	11
#define INT_HDLCTXA	12
#define INT_HDLCRXA	13
#define INT_HDLCTXB	14
#define INT_HDLCRXB	15
#define INT_BDMATX	16
#define INT_BDMARX	17
#define INT_MACTX	18
#define INT_MACRX	19
#define INT_IIC		20
#define INT_GLOBAL	21

#define IRQ_TIMER	INT_TIMER0

#endif /* __ASM_ARCH_IRQS_H__ */

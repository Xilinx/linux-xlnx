/*
 *  linux/include/asm-arm/arch-lpc22xx/irqs.h
 *
 *  Copyright (C) 2004 Philips Semiconductors
 *
 *  IRQ number definition
 *  All IRQ numbers of the LPC22xx CPUs.
 *
 */

#ifndef __LPC22xx_irqs_h
#define __LPC22xx_irqs_h                        1

#define NR_IRQS		28
	
#define LPC22xx_INTERRUPT_WDINT	 0	/* Watchdog int. 0 */
#define LPC22xx_INTERRUPT_RSV0	 1	/* Reserved int. 1 */
#define LPC22xx_INTERRUPT_DBGRX	 2	/* Embedded ICE DbgCommRx receive */
#define LPC22xx_INTERRUPT_DBGTX	 3	/* Embedded ICE DbgCommRx Transmit*/
#define LPC22xx_INTERRUPT_TIMER0 4	/* Timer 0 */
#define LPC22xx_INTERRUPT_TIMER1 5	/* Timer 1 */
#define LPC22xx_INTERRUPT_UART0	 6	/* UART 0 */
#define LPC22xx_INTERRUPT_UART1  7	/* UART 1 */
#define LPC22xx_INTERRUPT_PWM0 	 8	/* PWM */
#define LPC22xx_INTERRUPT_I2C 	 9	/* I2C  */
#define LPC22xx_INTERRUPT_SPI0 	10	/* SPI0 */
#define LPC22xx_INTERRUPT_SPI1 	11	/* SPI1 */
#define LPC22xx_INTERRUPT_PLL 	12	/* PLL */
#define LPC22xx_INTERRUPT_RTC 	13	/* RTC */
#define LPC22xx_INTERRUPT_EINT0	14	/* Externel Interrupt 0 */
#define LPC22xx_INTERRUPT_EINT1	15	/* Externel Interrupt 1 */
#define LPC22xx_INTERRUPT_EINT2	16	/* Externel Interrupt 2 */
#define LPC22xx_INTERRUPT_EINT3	17	/* Externel Interrupt 3 */
#define LPC22xx_INTERRUPT_ADC 	18	/* AD Converter */
#define LPC22xx_INTERRUPT_CANERR 19	/* CAN LUTerr interrupt */
#define LPC22xx_INTERRUPT_CAN1TX 20	/* CAN1 Tx interrupt */
#define LPC22xx_INTERRUPT_CAN1RX 21	/* CAN1 Rx interrupt */
#define LPC22xx_INTERRUPT_CAN2TX 22	/* CAN2 Tx interrupt */
#define LPC22xx_INTERRUPT_CAN2RX 23	/* CAN2 Rx interrupt */
#define LPC22xx_INTERRUPT_CAN3TX 24	/* CAN1 Tx interrupt */
#define LPC22xx_INTERRUPT_CAN3RX 25	/* CAN1 Rx interrupt */
#define LPC22xx_INTERRUPT_CAN4TX 26	/* CAN2 Tx interrupt */
#define LPC22xx_INTERRUPT_CAN4RX 27	/* CAN2 Rx interrupt */

#endif /* End of __irqs_h */

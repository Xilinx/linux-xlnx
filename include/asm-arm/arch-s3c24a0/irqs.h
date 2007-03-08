/*
 * include/asm-arm/arch-s3c24a0/irqs.h
 * 
 * $Id: irqs.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*                    +-+
 *                    |m|    +---------+
 *    sub-irq ------\ |a|--> | sub-irq |
 *    sources ------/ |s|    | status  |\
 *                    |k|    +---------+ \
 *                    +-+                 \
 *                                         \
 *                    +-+                   \      +-+
 *                    |m|    +---------+     +---> |m|     +--------+
 * external irq ----\ |a|--> | ext-irq |---------> |a|--\  |main-irq|
 *    sources   ----/ |s|    | status  |       +-> |s|--/  | status |
 *                    |k|    +---------+      /    |k|     +--------+
 *                    +-+                    /     +-+
 *                                          /
 *                                         /
 *                                        /
 *  irq sources -------------------------/
 *
 */


/*
 * We have three groups.
 * #0 : Normal (or Main) IRQs
 * #1 : Sub-IRQs
 * #2 : External IRQs
 */
#define NR_IRQ_GRP			(32)	/* number of irqs in group */
#define IRQ_GRP0_START		(0)
#define IRQ_GRP1_START		(IRQ_GRP0_START + NR_IRQ_GRP)
#define IRQ_GRP2_START		(IRQ_GRP1_START + NR_IRQ_GRP)

#define NR_IRQS				(NR_IRQ_GRP * 3)

#define SUBIRQ_ENC(x)		((x) + IRQ_GRP1_START)
#define SUBIRQ_DEC(x)		((x) - IRQ_GRP1_START)
#define EINTIRQ_ENC(x)		((x) + IRQ_GRP2_START)
#define EINTIRQ_DEC(x)		((x) - IRQ_GRP2_START)

/* Interrupt controller */
#define IRQ_EINT0_2			(0)		/* External interrupt 0 ~ 2 */
#define IRQ_EINT3_6			(1)		/* External interrupt 3 ~ 6 */
#define IRQ_EINT7_10			(2)		/* External interrupt 7 ~ 10 */
#define IRQ_EINT11_14			(3)		/* External interrupt 11 ~ 14 */
#define IRQ_EINT15_18			(4)		/* External interrupt 15 ~ 18 */
#define IRQ_TIC				(5)		/* RTC time tick */
#define IRQ_DCTQ			(6)		/* DCTQ */
#define IRQ_MC				(7)		/* MC */
#define IRQ_ME				(8)		/* ME */
#define IRQ_KEYPAD			(9)		/* Keypad */
#define IRQ_TIMER0			(10)	/* Timer 0 */
#define IRQ_TIMER1			(11)	/* Timer 1 */
#define IRQ_TIMER2			(12)	/* Timer 2 */
#define IRQ_TIMER3_4			(13)	/* Timer 3, 4 */
#define IRQ_LCD_POST			(14)	/* LCD/POST */
#define IRQ_CAM_C			(15)	/* Camera Codec */
#define IRQ_WDT_BATFLT			(16)	/* WDT/BATFLT */
#define IRQ_UART0			(17)	/* UART 0 */
#define IRQ_CAM_P			(18)	/* Camera Preview */
#define IRQ_MODEM			(19)	/* Modem */
#define IRQ_DMA				(20)	/* DMA channels for S-bus */
#define IRQ_SDI				(21)	/* SDI MMC */
#define IRQ_SPI0			(22)	/* SPI 0 */
#define IRQ_UART1			(23)	/* UART 1 */
#define IRQ_AC97_NFLASH			(24)	/* AC97/NFALASH */
#define IRQ_USBD			(25)	/* USB device */
#define IRQ_USBH			(26)	/* USB host */
#define IRQ_IIC				(27)	/* IIC */
#define IRQ_IRDA_MSTICK			(28)	/* IrDA/MSTICK */
#define IRQ_VLX_SPI1			(29)	/* SPI 1 */
#define IRQ_RTC				(30)	/* RTC alaram */
#define IRQ_ADC_PENUPDN			(31)	/* ADC EOC/Pen up/Pen down */

/* SUB IRQ */
#define IRQ_RXD0			SUBIRQ_ENC(0)
#define IRQ_TXD0			SUBIRQ_ENC(1)
#define IRQ_ERR0			SUBIRQ_ENC(2)
#define IRQ_RXD1			SUBIRQ_ENC(3)
#define IRQ_TXD1			SUBIRQ_ENC(4)
#define IRQ_ERR1			SUBIRQ_ENC(5)
#define IRQ_IRDA			SUBIRQ_ENC(6)
#define IRQ_MSTICK			SUBIRQ_ENC(7)
#define IRQ_TIMER3			SUBIRQ_ENC(11)
#define IRQ_TIMER4			SUBIRQ_ENC(12)
#define IRQ_WDT				SUBIRQ_ENC(13)
#define IRQ_BATFLT			SUBIRQ_ENC(14)
#define IRQ_POST			SUBIRQ_ENC(15)
#define IRQ_DISP_FIFO			SUBIRQ_ENC(16)
#define IRQ_PENUP			SUBIRQ_ENC(17)
#define IRQ_PENDN			SUBIRQ_ENC(18)
#define IRQ_ADC				SUBIRQ_ENC(19)
#define IRQ_DISP_FRAME			SUBIRQ_ENC(20)
#define IRQ_NFLASH			SUBIRQ_ENC(21)
#define IRQ_AC97			SUBIRQ_ENC(22)
#define IRQ_SPI1			SUBIRQ_ENC(23)
#define IRQ_VLX				SUBIRQ_ENC(24)
#define IRQ_DMA0			SUBIRQ_ENC(25)
#define IRQ_DMA1			SUBIRQ_ENC(26)
#define IRQ_DMA2			SUBIRQ_ENC(27)
#define IRQ_DMA3			SUBIRQ_ENC(28)

/* External IRQ */
#define IRQ_EINT0			EINTIRQ_ENC(0)
#define IRQ_EINT1			EINTIRQ_ENC(1)
#define IRQ_EINT2			EINTIRQ_ENC(2)
#define IRQ_EINT3			EINTIRQ_ENC(3)
#define IRQ_EINT4			EINTIRQ_ENC(4)
#define IRQ_EINT5			EINTIRQ_ENC(5)
#define IRQ_EINT6			EINTIRQ_ENC(6)
#define IRQ_EINT7			EINTIRQ_ENC(7)
#define IRQ_EINT8			EINTIRQ_ENC(8)
#define IRQ_EINT9			EINTIRQ_ENC(9)
#define IRQ_EINT10			EINTIRQ_ENC(10)
#define IRQ_EINT11			EINTIRQ_ENC(11)
#define IRQ_EINT12			EINTIRQ_ENC(12)
#define IRQ_EINT13			EINTIRQ_ENC(13)
#define IRQ_EINT14			EINTIRQ_ENC(14)
#define IRQ_EINT15			EINTIRQ_ENC(15)
#define IRQ_EINT16			EINTIRQ_ENC(16)
#define IRQ_EINT17			EINTIRQ_ENC(17)
#define IRQ_EINT18			EINTIRQ_ENC(18)
#define IRQ_EINT19			EINTIRQ_ENC(19)
#define IRQ_EINT20			EINTIRQ_ENC(20)
#define IRQ_EINT21			EINTIRQ_ENC(21)
#define IRQ_EINT22			EINTIRQ_ENC(22)
#define IRQ_EINT23			EINTIRQ_ENC(23)
#define IRQ_EINT24			EINTIRQ_ENC(24)
#define IRQ_EINT25			EINTIRQ_ENC(25)
#define IRQ_EINT26			EINTIRQ_ENC(26)
#define IRQ_EINT27			EINTIRQ_ENC(27)
#define IRQ_EINT28			EINTIRQ_ENC(28)
#define IRQ_EINT29			EINTIRQ_ENC(29)
#define IRQ_EINT30			EINTIRQ_ENC(30)
#define IRQ_EINT31			EINTIRQ_ENC(31)

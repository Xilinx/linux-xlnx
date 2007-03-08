/*
 *  linux/include/asm-armnommu/arch-s3c3410/irqs.h
 *
 * 2003 Thomas Eschenbacher <thomas.eschenbacher@gmx.de>
 *
 * All IRQ numbers of the S3C3410X CPUs.
 *
 */

#ifndef __S3C3410_irqs_h
#define __S3C3410_irqs_h                        1

#define NR_IRQS		32
	
#define S3C3410X_INTERRUPT_EINT0	 0	/* External int. 0 */
#define S3C3410X_INTERRUPT_EINT1	 1	/* External int. 1 */
#define S3C3410X_INTERRUPT_URX  	 2	/* UART receive */
#define S3C3410X_INTERRUPT_UTX  	 3	/* UART transmit */
#define S3C3410X_INTERRUPT_UERR 	 4	/* UART error */
#define S3C3410X_INTERRUPT_DMA0 	 5	/* DMA 0 */
#define S3C3410X_INTERRUPT_DMA1 	 6	/* DMA 1 */
#define S3C3410X_INTERRUPT_TOF0 	 7	/* Timer 0 overflow */
#define S3C3410X_INTERRUPT_TMC0 	 8	/* Timer 0 match/capture */
#define S3C3410X_INTERRUPT_TOF1 	 9	/* Timer 1 overflow */
#define S3C3410X_INTERRUPT_TMC1 	10	/* Timer 1 match/capture */
#define S3C3410X_INTERRUPT_TOF2 	11	/* Timer 2 overflow */
#define S3C3410X_INTERRUPT_TMC2 	12	/* Timer 2 match/capture */
#define S3C3410X_INTERRUPT_TOF3 	13	/* Timer 3 overflow */
#define S3C3410X_INTERRUPT_TMC3 	14	/* Timer 3 match/capture */
#define S3C3410X_INTERRUPT_TOF4 	15	/* Timer 4 overflow */
#define S3C3410X_INTERRUPT_TMC4 	16	/* Timer 4 match/capture */
#define S3C3410X_INTERRUPT_BT   	17	/* Basic Timer */
#define S3C3410X_INTERRUPT_SIO0 	18	/* SIO 0 */
#define S3C3410X_INTERRUPT_SIO1 	19	/* SIO 1 */
#define S3C3410X_INTERRUPT_IIC  	20	/* IIC */
#define S3C3410X_INTERRUPT_RTCA 	21	/* RTC alarm */
#define S3C3410X_INTERRUPT_RTCT 	22	/* RTC time (SEC/MIN/HOUR) */
#define S3C3410X_INTERRUPT_TF   	23	/* Timer4 FIFO interrupt */
#define S3C3410X_INTERRUPT_EINT2	24	/* External int. 2 */
#define S3C3410X_INTERRUPT_EINT3	25	/* External int. 3 */
#define S3C3410X_INTERRUPT_EINT4567	26	/* External int. 4/5/6/7 */
#define S3C3410X_INTERRUPT_ADC   	27	/* ADC interrupt */
#define S3C3410X_INTERRUPT_EINT8	28	/* External int. 8 */
#define S3C3410X_INTERRUPT_EINT9	29	/* External int. 9 */
#define S3C3410X_INTERRUPT_EINT10	30	/* External int. 10 */
#define S3C3410X_INTERRUPT_EINT11	31	/* External int. 11 */

#endif /* End of __irqs_h */

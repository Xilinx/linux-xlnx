/*
 ******************* AT91x40xxx ********************
 */

#define ARM_CLK	CONFIG_ARM_CLK

#define AT91_USART_CNT 2
#define AT91_USART0_BASE	(0xfffd0000)
#define AT91_USART1_BASE	(0xfffcc000)
#define AT91_TC_BASE		(0xfffe0000)
#define AIC_BASE		(0xfffff000)	
#define AT91_PIOA_BASE		(0xffff0000)
#define AT91_SF_CIDR		(0xfff00000)

#define HARD_RESET_NOW()

#define HW_AT91_TIMER_INIT(timer)	/* no PMC */

/* use TC0 as hardware timer to create high resolution timestamps for debugging.
 *  Timer 0 must be set up as a free running counter, e.g. in the bootloader
 */
#define HW_COUNTER  (((struct at91_timers *)AT91_TC_BASE)->chans[0].ch.cv)

/* enable US0,US1 */
#define HW_AT91_USART_INIT ((volatile struct pio_regs *)AT91_PIOA_BASE)->pdr = \
				PIOA_RXD0|PIOA_TXD0|PIOA_RXD1|PIOA_TXD1; 
/* PIOA bit allocation */
#define PIOA_TCLK0	(1<<0)					
#define PIOA_TI0A0	(1<<1)					
#define PIOA_TI0B0	(1<<2)					
#define PIOA_TCLK1	(1<<3)					
#define PIOA_TIOA1	(1<<4)				
#define PIOA_TIOB1	(1<<5)				
#define PIOA_TCLK2	(1<<6)					
#define PIOA_TIOA2	(1<<7)				
#define PIOA_TIOB2	(1<<8)				
#define PIOA_IRQ0	(1<<9)				
#define PIOA_IRQ1	(1<<10)				
#define PIOA_IRQ2	(1<<11)				
#define PIOA_FIQ	(1<<12)					
#define PIOA_SCK0	(1<<13)					
#define PIOA_TXD0	(1<<14)					
#define PIOA_RXD0	(1<<15)

#define PIOA_SCK1	(1<<20)					
#define PIOA_TXD1	(1<<21)					
#define PIOA_RXD1	(1<<22)

#define PIOA_MCK0	(1<<25)	
#define PIOA_NCS2	(1<<26)
#define PIOA_NCS3	(1<<27)	

#define PIOA_A20_CS7	(1<<28)
#define PIOA_A21_CS6	(1<<29)	
#define PIOA_A22_CS5	(1<<30)
#define PIOA_A23_CS4	(1<<31)


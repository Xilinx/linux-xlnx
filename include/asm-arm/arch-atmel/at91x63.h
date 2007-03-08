/*
 ******************* AT91x63xxx ********************
 */

#define ARM_CLK		CONFIG_ARM_CLK

#define AT91_USART_CNT 2
#define AT91_USART0_BASE	(0xfffc0000)
#define AT91_USART1_BASE	(0xfffc4000)
#define AT91_TC_BASE		(0xfffd0000)
#define AIC_BASE		(0xfffff000)
#define AT91_PIOA_BASE 		(0xfffec000)
#define AT91_PIOB_BASE 		(0xffff0000)
#define AT91_PMC_BASE		(0xffff4000)

/* enable US0,US1 */
#define HW_AT91_USART_INIT ((volatile struct pmc_regs *)AT91_PMC_BASE)->pcer = \
				(1<<2) | (1<<3) | (1<<13); \
			   ((volatile struct pio_regs *)AT91_PIOA_BASE)->pdr = \
				PIOA_RXD0|PIOA_TXD0|PIOA_RXD1|PIOA_TXD1; 

#define HW_AT91_TIMER_INIT(timer) ((volatile struct pmc_regs *)AT91_PMC_BASE)->pcer = \
				1<<(timer+6);

/* PIOA bit allocation */
#define PIOA_TCLK3	(1<<0)					
#define PIOA_TI0A3	(1<<1)					
#define PIOA_TI0B3	(1<<2)					
#define PIOA_TCLK4	(1<<3)					
#define PIOA_TI0A4	(1<<4)					
#define PIOA_TI0B4	(1<<5)					
#define PIOA_TCLK5	(1<<6)					
#define PIOA_TI0A5	(1<<7)					
#define PIOA_TI0B5	(1<<8)					
#define PIOA_IRQ0	(1<<9)
#define PIOA_IRQ1	(1<<10)
#define PIOA_IRQ2	(1<<11)
#define PIOA_IRQ3	(1<<12)
#define PIOA_FIQ	(1<<13)
#define PIOA_SCK0	(1<<14)	
#define PIOA_TXD0	(1<<15)
#define PIOA_RXD0	(1<<16)
#define PIOA_SCK1	(1<<17)	
#define PIOA_TXD1	(1<<18)
#define PIOA_RXD1	(1<<19)
#define PIOA_SCK2	(1<<20)	
#define PIOA_TXD2	(1<<21)
#define PIOA_RXD2	(1<<22)
#define PIOA_SPCK	(1<<23)					
#define PIOA_MISO	(1<<24)					
#define PIOA_MOSI	(1<<25)					
#define PIOA_NPCS0	(1<<26)					
#define PIOA_NPCS1	(1<<27)					
#define PIOA_NPCS2	(1<<28)					
#define PIOA_NPCS3	(1<<29)					

/* PIOB bit allocation */
#define PIOB_MPI_NOE	(1<<0)					
#define PIOB_MPI_NLB	(1<<1)				
#define PIOB_MPI_NUB	(1<<2)				

#define PIOB_MCK0	(1<<17)				
#define PIOB_BMS	(1<<18)				
#define PIOB_TCLK0	(1<<19)				
#define PIOB_TIOA0	(1<<20)				
#define PIOB_TIOB0	(1<<21)				
#define PIOB_TCLK1	(1<<22)				
#define PIOB_TIOA1	(1<<23)				
#define PIOB_TIOB1	(1<<24)				
#define PIOB_TCLK2	(1<<25)				
#define PIOB_TIOA2	(1<<26)				
#define PIOB_TIOB2	(1<<27)		


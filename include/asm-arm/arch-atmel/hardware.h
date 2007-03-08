/*
 * linux/include/asm-arm/arch-atmel/hardware.h
 *
 * for Atmel AT91 series
 * 2001 Erwin Authried
 * 
 * modified for linux 2.6 by Hyok S. Choi, 2004
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

#ifndef __ASSEMBLY__

/* the machine dependent  bootmem reserve and free routines */
#define MACH_RESERVE_BOOTMEM()
#define MACH_FREE_BOOTMEM()

/* yes, freeing initmem is okay */
#define DO_FREE_INITMEM() 	(1)

#endif

#define ATMEL_MEM_SIZE     (CONFIG_DRAM_SIZE) 
#define MEM_SIZE            ATMEL_MEM_SIZE
#define PA_SDRAM_BASE         CONFIG_DRAM_BASE

/* 0=TC0, 1=TC1, 2=TC2 */
#define KERNEL_TIMER 1	

#ifdef CONFIG_CPU_AT91X40
#include "at91x40.h"
#elif CONFIG_CPU_AT91X63
#include "at91x63.h"
#else 
  #error "Configuration error: No CPU defined"
#endif

/*
 ******************* COMMON PART ********************
 */
#define AIC_SMR(i)  (AIC_BASE+i*4)
#define AIC_IVR	    (AIC_BASE+0x100)
#define AIC_FVR	    (AIC_BASE+0x104)
#define AIC_ISR	    (AIC_BASE+0x108)
#define AIC_IPR	    (AIC_BASE+0x10C)
#define AIC_IMR	    (AIC_BASE+0x110)
#define AIC_CISR	(AIC_BASE+0x114)
#define AIC_IECR	(AIC_BASE+0x120)
#define AIC_IDCR	(AIC_BASE+0x124)
#define AIC_ICCR	(AIC_BASE+0x128)
#define AIC_ISCR	(AIC_BASE+0x12C)
#define AIC_EOICR   (AIC_BASE+0x130)


#ifndef __ASSEMBLER__
struct at91_timer_channel
{
	unsigned long ccr;				// channel control register		(WO)
	unsigned long cmr;				// channel mode register		(RW)
	unsigned long reserved[2];		
	unsigned long cv;				// counter value				(RW)
	unsigned long ra;				// register A					(RW)
	unsigned long rb;				// register B					(RW)
	unsigned long rc;				// register C					(RW)
	unsigned long sr;				// status register				(RO)
	unsigned long ier;				// interrupt enable register	(WO)
	unsigned long idr;				// interrupt disable register	(WO)
	unsigned long imr;				// interrupt mask register		(RO)
};

struct at91_timers
{
	struct {
		struct at91_timer_channel ch;
		unsigned char padding[0x40-sizeof(struct at91_timer_channel)];
	} chans[3];
	unsigned  long bcr;				// block control register		(WO)
	unsigned  long bmr;				// block mode	 register		(RW)
};
#endif

/*  TC control register */
#define TC_SYNC	(1)

/*  TC mode register */
#define TC2XC2S(x)	(x & 0x3)
#define TC1XC1S(x)	(x<<2 & 0xc)
#define TC0XC0S(x)	(x<<4 & 0x30)
#define TCNXCNS(timer,v) ((v) << (timer<<1))

/* TC channel control */
#define TC_CLKEN	(1)			
#define TC_CLKDIS	(1<<1)			
#define TC_SWTRG	(1<<2)			

/* TC interrupts enable/disable/mask and status registers */
#define TC_MTIOB	(1<<18)
#define TC_MTIOA	(1<<17)
#define TC_CLKSTA	(1<<16)

#define TC_ETRGS	(1<<7)
#define TC_LDRBS	(1<<6)
#define TC_LDRAS	(1<<5)
#define TC_CPCS		(1<<4)
#define TC_CPBS		(1<<3)
#define TC_CPAS		(1<<2)
#define TC_LOVRS	(1<<1)
#define TC_COVFS	(1)

/*
 *	USART registers
 */


/*  US control register */
#define US_SENDA	(1<<12)
#define US_STTO		(1<<11)
#define US_STPBRK	(1<<10)
#define US_STTBRK	(1<<9)
#define US_RSTSTA	(1<<8)
#define US_TXDIS	(1<<7)
#define US_TXEN		(1<<6)
#define US_RXDIS	(1<<5)
#define US_RXEN		(1<<4)
#define US_RSTTX	(1<<3)
#define US_RSTRX	(1<<2)

/* US mode register */
#define US_CLK0		(1<<18)
#define US_MODE9	(1<<17)
#define US_CHMODE(x)(x<<14 & 0xc000)
#define US_NBSTOP(x)(x<<12 & 0x3000)
#define US_PAR(x)	(x<<9 & 0xe00)
#define US_SYNC		(1<<8)
#define US_CHRL(x)	(x<<6 & 0xc0)
#define US_USCLKS(x)(x<<4 & 0x30)

/* US interrupts enable/disable/mask and status register */
#define US_DMSI		(1<<10)
#define US_TXEMPTY	(1<<9)
#define US_TIMEOUT	(1<<8)
#define US_PARE		(1<<7)
#define US_FRAME	(1<<6)
#define US_OVRE		(1<<5)
#define US_ENDTX	(1<<4)
#define US_ENDRX	(1<<3)
#define US_RXBRK	(1<<2)
#define US_TXRDY	(1<<1)
#define US_RXRDY	(1)

#define US_ALL_INTS (US_DMSI|US_TXEMPTY|US_TIMEOUT|US_PARE|US_FRAME|US_OVRE|US_ENDTX|US_ENDRX|US_RXBRK|US_TXRDY|US_RXRDY)

#ifndef __ASSEMBLER__
struct atmel_usart_regs{
	unsigned long cr;		// control 
	unsigned long mr;		// mode
	unsigned long ier;		// interrupt enable
	unsigned long idr;		// interrupt disable
	unsigned long imr;		// interrupt mask
	unsigned long csr;		// channel status
	unsigned long rhr;		// receive holding 
	unsigned long thr;		// tramsmit holding		
	unsigned long brgr;		// baud rate generator		
	unsigned long rtor;		// rx time-out
	unsigned long ttgr;		// tx time-guard
	unsigned long res1;
	unsigned long rpr;		// rx pointer
	unsigned long rcr;		// rx counter
	unsigned long tpr;		// tx pointer
	unsigned long tcr;		// tx counter
};

static inline void at91_usart_init(volatile struct atmel_usart_regs *uart, int baudrate)
{

        uart->cr = US_TXDIS | US_RXDIS | US_RSTTX | US_RSTRX;
        /* clear Rx receive and Tx sent counters */
        uart->rcr = 0;
        uart->tcr = 0;

	uart->idr = US_TXEMPTY;		/* tx disable */
	uart->idr = US_ENDRX | US_TIMEOUT; /* rx disable */
	
        /* Set the serial port into a safe sane state */
        uart->mr = US_USCLKS(0) | US_CLK0 | US_CHMODE(0) | US_NBSTOP(0) |
                    US_PAR(4) | US_CHRL(3);

        /* FIXME: uart->brgr = ARM_CLK/16/baudrate; */
        uart->brgr = ARM_CLK/16/9600;

        uart->rtor = 20;                        // timeout = value * 4 *bit period
        uart->ttgr = 0;                         // no guard time
        uart->rcr = 0;
        uart->rpr = 0;
        uart->tcr = 0;
        uart->tpr = 0;
#ifdef US_RTS
        uart->mc = 0;
#endif
}

static inline void at91_usart_putc(volatile struct atmel_usart_regs *uart, unsigned char c)
{
       uart->cr=US_TXEN;
       uart->thr=c;
       while(1) {
                if (uart->csr & US_TXEMPTY) break;
       }
}
#endif
		
#define PIO(i)		(1<<i)

#ifndef __ASSEMBLER__
struct pio_regs{
	unsigned long per;
	unsigned long pdr;
	unsigned long psr;
	unsigned long res1;
	unsigned long oer;
	unsigned long odr;
	unsigned long osr;
	unsigned long res2;
	unsigned long ifer;
	unsigned long ifdr;
	unsigned long ifsr;
	unsigned long res3;
	unsigned long sodr;
	unsigned long codr;
	unsigned long odsr;
	unsigned long pdsr;
	unsigned long ier;
	unsigned long idr;
	unsigned long imr;
	unsigned long isr;
};
#endif

#ifndef __ASSEMBLER__
struct pmc_regs{
	unsigned long scer;
	unsigned long scdr;
	unsigned long scsr;
	unsigned long reserved;
	unsigned long pcer;
	unsigned long pcdr;
	unsigned long pcsr;
};
#endif

#endif  /* _ASM_ARCH_HARDWARE_H */



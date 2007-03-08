/***************************************************************************/

/*
 *	linux/drivers/char/resetswitch.c
 *
 *	Basic driver to support the NETtel software reset button.
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>
#include <asm/delay.h>

/***************************************************************************/
#ifdef CONFIG_M5272
/***************************************************************************/

/*
 *	Off-course the 5272 would have to deal with setting up and
 *	acknowledging interrupts differently to all other ColdFIre's!
 */
#define	SWITCH_IRQ	65

static void __inline__ mcf_enablevector(int vecnr)
{
        volatile unsigned long  *icrp;
        icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
        *icrp = (*icrp & 0x07777777) | 0xf0000000;
}

static void __inline__ mcf_disablevector(void)
{
	volatile unsigned long  *icrp;
	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = (*icrp & 0x07777777) | 0x80000000;
}

static void __inline__ mcf_ackvector(void)
{
	volatile unsigned long  *icrp;
	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = (*icrp & 0x07777777) | 0xf0000000;
}

static __inline__ int mcf_isvector(void)
{
	return((*((volatile unsigned long *) (MCF_MBAR + MCFSIM_ISR)) & 0x80000000) == 0);
}

/***************************************************************************/
#else
/***************************************************************************/

/*
 *	Common vector setup for 5206e, 5307 and 5407.
 */
#define	SWITCH_IRQ	31

static void __inline__ mcf_enablevector(int vecnr)
{
}

static void __inline__ mcf_disablevec(void)
{
	mcf_setimr(mcf_getimr() | MCFSIM_IMR_EINT7);
}

static void __inline__ mcf_ackvector(void)
{
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_EINT7);
}

static __inline__ int mcf_isvector(void)
{
	return(mcf_getipr() & MCFSIM_IMR_EINT7);
}

/***************************************************************************/
#endif /* !CONFIG_M5272 */
/***************************************************************************/

void resetswitch_button(int irq, void *dev_id, struct pt_regs *regs)
{
	extern void	flash_eraseconfig(void);
	static int	inbutton = 0;

	/*
	 *	IRQ7 is not maskable by the CPU core. It is possible
	 *	that switch bounce mey get us back here before we have
	 *	really serviced the interrupt.
	 */
	if (inbutton)
		return;
	inbutton = 1;

	/* Disable interrupt at SIM - best we can do... */
#if 0
	mcf_disablevec();
#endif

	/* Try and de-bounce the switch a little... */
	udelay(10000);

#ifdef CONFIG_BLK_DEV_BLKMEM
	flash_eraseconfig();
#endif

	/* Don't leave here 'till button is no longer pushed! */
	for (;;) {
		if (mcf_isvector() != 0)
			break;
	}

	HARD_RESET_NOW();
	/* Should never get here... */

	inbutton = 0;

	mcf_ackvector();
}

/***************************************************************************/

int resetswitch_init(void)
{
	mcf_enablevector(SWITCH_IRQ);
	mcf_autovector(SWITCH_IRQ);
	request_irq(SWITCH_IRQ, resetswitch_button,
		(SA_INTERRUPT | IRQ_FLG_FAST), "Reset Button", NULL);
	return(0);
}

module_init(resetswitch_init);

/***************************************************************************/

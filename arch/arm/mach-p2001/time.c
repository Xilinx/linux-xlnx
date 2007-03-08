/*
 *  linux/arch/arm/mach-p2001/time.c
 *
 *  Copyright (C) 2004-2005 Tobias Lorenz
 *
 *  Timer handling code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#ifdef CONFIG_CPU_FREQ
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#endif

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/io.h>

#include <asm/mach/time.h>

#define P2001_TIMER_VALUE(reg, mask, shift, value) { \
	unsigned int i = (P2001_TIMER->reg); \
	i &= ~((mask) << (shift)); \
	i |= (((value) & (mask)) << (shift)); \
	(P2001_TIMER->reg) = i; \
}

/*
 * short calculation
 * ---------------------------------------------------------------
 * prescaler = factor * (SYSCLK / 12288000)		max: 255
 * period = SYSCLK/prescaler/HZ				max: 65535
 * clocks_per_usec = SYSCLK/prescaler / 1000000		min: 1
 *                 = 12288000 / factor / 1000000
 * ---------------------------------------------------------------
 * IMPORTANT: recalculate factor when HZ changes, so that limits
 * are kept within SYSCLK range (12288000-73728000)
 */

/**************************************************************************
 * Timer 1: Scheduler
 **************************************************************************/
#define TIMER1_HZ	HZ	/* 100-1000 HZ */
#define TIMER1_FACTOR	2

static irqreturn_t p2001_timer1_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	timer_tick();

	/* clear interrupt pending bit */
	P2001_TIMER->TIMER_INT &= ~(1<<0);		// Timer1_Int

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction p2001_timer1_irq = {
	.name		= "P2001 timer1",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= p2001_timer1_interrupt,
};

/* Return number of microseconds since last interrupt */
#define TIMER1_CLOCKS_PER_USEC (12288000/TIMER1_FACTOR/1000000)
static unsigned long p2001_gettimeoffset(void)
{
	return ((0xffff - P2001_TIMER->Timer1) & 0xffff) / TIMER1_CLOCKS_PER_USEC;
}

#ifdef CONFIG_CPU_FREQ
/*
 * Transistion notifier
 */
static int p2001_timer1_notifier(struct notifier_block *self, unsigned long phase, void *data)
{
	struct cpufreq_freqs *cf = data;
	unsigned int prescaler, period;

	if ((phase == CPUFREQ_POSTCHANGE) ||
	    (phase == CPUFREQ_RESUMECHANGE)) {
	    	prescaler = TIMER1_FACTOR*cf->new/12288;
		period = (1000*cf->new/prescaler)/TIMER1_HZ;
		P2001_TIMER_VALUE(TIMER_PRELOAD, 0xffff, 0, period);
		P2001_TIMER_VALUE(Timer12_PreDiv, 0xff, 0, prescaler - 1);
	}

	return NOTIFY_OK;
}

static struct notifier_block p2001_timer1_nb = { &p2001_timer1_notifier, NULL, 0 };
#endif /* CONFIG_CPU_FREQ */

static void p2001_timer1_init(void)
{
	unsigned int prescaler, period;

	/* initialize the timer period and prescaler */
	prescaler = TIMER1_FACTOR*(CONFIG_SYSCLK/12288000);
	period = (CONFIG_SYSCLK/prescaler)/TIMER1_HZ;
	P2001_TIMER_VALUE(TIMER_PRELOAD, 0xffff, 0, period);
	P2001_TIMER_VALUE(Timer12_PreDiv, 0xff, 0, prescaler - 1);

	/* set up the interrupt vector for timer 1 match */
	setup_irq(IRQ_TIMER1, &p2001_timer1_irq);
	
	/* enable the timer IRQ */
	P2001_TIMER->TIMER_INT |= (1<<4);		// Timer1_Int_En

	/* let timer 1 run... */
	P2001_TIMER->Timer12_PreDiv &= ~(1<<28);	// Timer_1_Disable

#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&p2001_timer1_nb, CPUFREQ_TRANSITION_NOTIFIER);
#endif
}


/**************************************************************************
 * Timer 2: LED Frequency Indicator
 **************************************************************************/
#ifdef CONFIG_P2001_TIMER2_LED_FREQ_INDICATOR
#define TIMER2_HZ	10
#define TIMER2_FACTOR	20

static irqreturn_t p2001_timer2_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int gpio2;

	/* switch leds */
	gpio2 = P2001_GPIO->GPIO2_Out;
	if (gpio2 & 0x0040) {
		/* gpio23_v4 on */
		gpio2 &= ~0x00c0;
		gpio2 |= 0x0080;
	} else {
		/* gpio22_v5 on */
		gpio2 &= ~0x00c0;
		gpio2 |= 0x0040;
	}
	P2001_GPIO->GPIO2_Out = gpio2;

	/* clear interrupt pending bit */
	P2001_TIMER->TIMER_INT &= ~(1<<1);		// Timer2_Int

	return IRQ_HANDLED;
}

static struct irqaction p2001_timer2_irq = {
	.name		= "P2001 timer2",
	.flags		= SA_INTERRUPT,
	.handler	= p2001_timer2_interrupt,
};

static void p2001_timer2_init(void)
{
	unsigned int prescaler, period;

	/* initialize the timer period and prescaler */
	prescaler = TIMER2_FACTOR*(CONFIG_SYSCLK/12288000);
	period = (CONFIG_SYSCLK/prescaler)/TIMER2_HZ;
	P2001_TIMER_VALUE(TIMER_PRELOAD, 0xffff, 16, period);
	P2001_TIMER_VALUE(Timer12_PreDiv, 0xff, 8, prescaler - 1);

	/* Activate Leds Frequency Indicator */
	/* Schematics say that: SDO_2/GPIO_22=V5, SDI_2/GPIO_23=V4 */
	P2001_GPIO->PIN_MUX |= (1<<2);			// set MUX to GPIOs
	P2001_GPIO->GPIO2_En |= 0xC0;			// Enable GPIO driver
	P2001_GPIO->GPIO2_Out |= 0x00C00000; 		// Mask bits

	/* set up the interrupt vector for timer 2 match */
	setup_irq(IRQ_TIMER2, &p2001_timer2_irq);

	/* enable the timer IRQ */
	P2001_TIMER->TIMER_INT |= (1<<5);		// Timer2_Int_En

	/* let timer 2 run... */
	P2001_TIMER->Timer12_PreDiv &= ~(1<<29);	// Timer_2_Disable
}
#endif


/**************************************************************************
 * Watchdog
 **************************************************************************/
#ifdef CONFIG_P2001_WATCHDOG
static irqreturn_t p2001_wdt_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	// printk(KERN_CRIT "Critical watchdog value reached: %d!\n", P2001_TIMER->WatchDog_Timer);

	/* Reset watchdog */
	P2001_TIMER->Timer12_PreDiv |= (1<<31);		// WatchDog_Reset

	/* clear interrupt pending bit */
	P2001_TIMER->TIMER_INT &= ~(1<<2);		// WatchDog_Int

	return IRQ_HANDLED;
}

static struct irqaction p2001_wdt_irq = {
	.name		= "P2001 watchdog",
	.flags		= SA_INTERRUPT,
	.handler	= p2001_wdt_interrupt,
};

static void p2001_wdt_init(void)
{
	/* Set predivider, so that watchdog runs at 3000 Hz */
	/* Reset after 65536/3000 = 21.85 secs (75 MHz) */
	P2001_TIMER->Timer12_PreDiv |= (0xfff << 16);	// PreDiv_WatchDog

	/* Reset watchdog */
	P2001_TIMER->Timer12_PreDiv |= (1<<31);		// WatchDog_Reset

	/* Warning after 30000/3000 = 10 secs passed */
	P2001_TIMER->TIMER_INT |= (1<<6);		// WatchDog_Int_En
	P2001_TIMER->TIMER_INT &= 0xff;			// WatchDog_Int_Level
	P2001_TIMER->TIMER_INT |= (30000 << 8);		// WatchDog_Int_Level

	/* Activate watchdog warning interrupt */
	setup_irq(IRQ_WATCHDOG, &p2001_wdt_irq);

	/* Activate watchdog */
	P2001_TIMER->Timer12_PreDiv &= ~(1<<30);	// WatchDog_Disable
}
#endif


/**************************************************************************
 * Main init
 **************************************************************************/
static void __init p2001_init_time(void)
{
	/*
	 * disable and clear timer 0, set to
	 * internal clock and interval mode
	 */
	P2001_TIMER->Timer12_PreDiv = 0x70bb0000;
	P2001_TIMER->Timer1 = 0;
	P2001_TIMER->Timer2 = 0;

	p2001_timer1_init();
#ifdef CONFIG_P2001_TIMER2_LED_FREQ_INDICATOR
	p2001_timer2_init();
#endif
#ifdef CONFIG_P2001_WATCHDOG
	p2001_wdt_init();
#endif
}

struct sys_timer p2001_timer = {
	.init	= p2001_init_time,
	.offset	= p2001_gettimeoffset,
};

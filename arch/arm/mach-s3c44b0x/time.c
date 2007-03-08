/*
 * for 2.6.8.1 port by 
 *    Hyok S. Choi <hyok.choi@samsung.com>
 * linux/arch/armnommu/mach-s3c44b0x/time.c
 */

#include <linux/init.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/mach/time.h>
#include <asm/arch/timex.h>

#define S3C44B0X_SYSTIMER_DIVIDER	2
extern int s3c44b0x_fMHZ;
extern int s3c44b0x_finMHZ;

/* the system clock is in MHz unit, here I use the prescale value for 1 us resolution */

#if	CONFIG_ARM_CLK_ADJUST
void s3c44b0x_systimer_setup(void)
#else
void __init s3c44b0x_systimer_setup(void)
#endif
{
	int prescale = s3c44b0x_fMHZ / S3C44B0X_SYSTIMER_DIVIDER;
	int cnt = s3c44b0x_fMHZ * 1000000 / prescale / S3C44B0X_SYSTIMER_DIVIDER / HZ;
	
	SYSREG_CLR	(S3C44B0X_TCON,0x7<<24);			// stop timer 5			
	SYSREG_SET	(S3C44B0X_TCNTB5, cnt);
	SYSREG_OR_SET	(S3C44B0X_TCON, 2<<24);				// update timer5 counter
	
	SYSREG_OR_SET	(S3C44B0X_TCFG0, (prescale - 1) << 16);		// set prescale, bit 16-23
	SYSREG_AND_SET	(S3C44B0X_TCFG1, 0xff0fffff);			// set timer5 divider, bit 20-23.  0 for 1/2 
}

void __inline__ s3c44b0x_systimer_start(void)
{
	SYSREG_CLR	(S3C44B0X_TCON, 0x02<<24);
	SYSREG_OR_SET	(S3C44B0X_TCON, 0x05<<24);
}

/*
 * Set up timer interrupt.
 */
#if     CONFIG_ARM_CLK_ADJUST
void s3c44b0x_led_off(int);
void s3c44b0x_led_on(int);
#endif
                                                                                                                                           
unsigned long s3c44b0x_gettimeoffset (void)
{
        return SYSREG_GETW(S3C44B0X_TCNTB5);
}
                                                                                                                                           
static irqreturn_t s3c44b0x_timer_interrupt(int irq, void *dev_id)
{
#if     CONFIG_DEBUG_NICKMIT
        static int cnt = 0;
        ++cnt;
        if (cnt == HZ) {
                static int stat = 0;
                cnt = 0;
                if (stat)
                        s3c44b0x_led_on(0);
                else
                        s3c44b0x_led_off(0);
                stat = 1 - stat;
        }
#endif
        timer_tick();

        return IRQ_HANDLED;
}

static struct irqaction s3c44b0x_timer_irq = {
        .name           = "S3C44B0X Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
        .handler        = s3c44b0x_timer_interrupt
};

                                                                                                                                           
void __init s3c44b0x_time_init(void)
{
        s3c44b0x_systimer_setup();
        /*
         * @todo do those really need to be function pointers ?
         */
        gettimeoffset     = s3c44b0x_gettimeoffset;
        s3c44b0x_timer_irq.handler = s3c44b0x_timer_interrupt;
                                                                                                                                           
        setup_irq(S3C44B0X_INTERRUPT_TIMER5, &s3c44b0x_timer_irq);
        s3c44b0x_clear_pb(S3C44B0X_INTERRUPT_TIMER5);
        s3c44b0x_unmask_irq(S3C44B0X_INTERRUPT_TIMER5);
                                                                                                                                           
        s3c44b0x_systimer_start();
}


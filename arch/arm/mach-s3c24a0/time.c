/*
 * time.c for smdk24a0
 * by Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on linux/include/asm-arm/arch-s3c2410/time.h
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/time.h>
#include <asm/arch/time.h>

/* copy from linux/arch/arm/kernel/time.c */
#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

#ifndef RTC_LEAP_YEAR
#define RTC_LEAP_YEAR        2000
#endif

extern spinlock_t rtc_lock;

unsigned long elfin_get_rtc_time(void)
{
        unsigned int year, mon, day, hour, min, sec;

        spin_lock_irq(&rtc_lock);
read_rtc_bcd_time:
        year = BCDYEAR & Msk_RTCYEAR;
        mon  = BCDMON  & Msk_RTCMON;
        day  = BCDDAY  & Msk_RTCDAY;
        hour = BCDHOUR & Msk_RTCHOUR;
        min  = BCDMIN  & Msk_RTCMIN;
        sec  = BCDSEC  & Msk_RTCSEC;
        if (sec == 0) {
                /* If BCDSEC is zero, reread all bcd registers.
                   See Section 17.2 READ/WRITE REGISTERS for more info. */
                goto read_rtc_bcd_time;
        }
        spin_unlock_irq(&rtc_lock);

        BCD_TO_BIN(year);
        BCD_TO_BIN(mon);
        BCD_TO_BIN(day);
        BCD_TO_BIN(hour);
        BCD_TO_BIN(min);
        BCD_TO_BIN(sec);

        year += RTC_LEAP_YEAR;

        return (mktime(year, mon, day, hour, min, sec));
}

/*
 * Copyed from drivers/char/sa1100-rtc.c.
 */
#define epoch                   1970

static const unsigned char days_in_mo[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#ifndef is_leap
#define is_leap(year) \
        ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif

/*
 * Converts seconds since 1970-01-01 00:00:00 to Gregorian date.
 */
static void decodetime (unsigned long t, struct rtc_time *tval)
{
        unsigned long days, month, year, rem;

        days = t / 86400;
        rem = t % 86400;
        tval->tm_hour = rem / 3600;
        rem %= 3600;
        tval->tm_min = rem / 60;
        tval->tm_sec = rem % 60;
        tval->tm_wday = (4 + days) % 7;

#define LEAPS_THRU_END_OF(y) ((y)/4 - (y)/100 + (y)/400)

        year = epoch;
        while (days >= (365 + is_leap(year))) {
                unsigned long yg = year + days / 365;
                days -= ((yg - year) * 365
                                + LEAPS_THRU_END_OF (yg - 1)
                                - LEAPS_THRU_END_OF (year - 1));
                year = yg;
        }
        tval->tm_year = year - 1900;
        tval->tm_yday = days + 1;

        month = 0;
        if (days >= 31) {
                days -= 31;
                month++;
                if (days >= (28 + is_leap(year))) {
                        days -= (28 + is_leap(year));
                        month++;
                        while (days >= days_in_mo[month]) {
                                days -= days_in_mo[month];
                                month++;
                        }
                }
        }
        tval->tm_mon = month;
        tval->tm_mday = days + 1;
}

int elfin_set_rtc(void)
{
        unsigned long current_time = xtime.tv_sec;
        unsigned char year, mon, day, hour, min, sec;
        signed int yeardiff;
        struct rtc_time rtc_tm;

        decodetime(current_time, &rtc_tm);

        yeardiff = (rtc_tm.tm_year + 1900) - RTC_LEAP_YEAR;
        if (yeardiff < 0) {
                /* S3C2410 RTC forces that the year must be higher or
                   equal than 2000, so initailize it. */
                yeardiff = 0;
        }

        year = (unsigned char) yeardiff;
        mon = rtc_tm.tm_mon + 1; /* tm_mon starts at zero */
        day = rtc_tm.tm_mday;
        hour = rtc_tm.tm_hour;
        min = rtc_tm.tm_min;
        sec = rtc_tm.tm_sec;

        BIN_TO_BCD(sec);
        BIN_TO_BCD(min);
        BIN_TO_BCD(hour);
        BIN_TO_BCD(day);
        BIN_TO_BCD(mon);
        BIN_TO_BCD(year);

        spin_lock_irq(&rtc_lock);
        RTCCON |= RTCCON_EN;
        BCDSEC  = sec  & Msk_RTCSEC;
        BCDMIN  = min  & Msk_RTCMIN;
        BCDHOUR = hour & Msk_RTCHOUR;
        BCDDAY  = day  & Msk_RTCDAY;
        BCDMON  = mon  & Msk_RTCMON;
        BCDYEAR = year & Msk_RTCYEAR;
        RTCCON &= ~RTCCON_EN;
        spin_unlock_irq(&rtc_lock);

        return 0;
}

static unsigned long elfin_gettimeoffset(void)
{
        unsigned long elapsed, usec;
        unsigned long latch;

        /* Use TCNTB4 as LATCH */
        latch = TCNTB4;

        elapsed = latch - TCNTO4;
        usec = (elapsed * (tick_nsec / 1000)) / latch; // hcyun

        return usec;
}

static irqreturn_t elfin_timer_interrupt(int irq, void *dev_id)
{

//      do_set_rtc();
	timer_tick();

        return IRQ_HANDLED;

}

static struct irqaction elfin_timer_irq = {
	.name		= "S3C24A0 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= elfin_timer_interrupt
};

#define TCON4_PRESCALER_VALUE   15
#define TCON4_DIVIDER_VALUE     2
#define TCON4_PERIOD            10      /* miliseconds */
void __init elfin_time_init(void)
{
        set_rtc = elfin_set_rtc;
        xtime.tv_sec = elfin_get_rtc_time();

        /* set timer interrupt */
        TCFG0 = (TCFG0_DZONE(0) | TCFG0_PRE1(TCON4_PRESCALER_VALUE) | TCFG0_PRE0(TCON4_PRESCALER_VALUE));

        /*
         * period = (prescaler value + 1) * (divider value) * buffer count / PCLK
         * buffer count = period * PCLK / divider value / (prescaler value + 1)
         *
         * e.g.)
         *
         * PCLK = 50700000 Hz, divider value = 2, prescaler value = 15
         * period = 10ms
         *
         * buffer count = ((10 / 1000) * 50700000) / 2 / (15+1)
         *              = 15843.75
         */

        printk("DEBUG: PCLK=%d, Prescaler=%d, Divider=%d\n", elfin_get_bus_clk(GET_PCLK), TCON4_PRESCALER_VALUE+1, TCON4_DIVIDER_VALUE );
        TCNTB4 = ((TCON4_PERIOD * ((elfin_get_bus_clk(GET_PCLK))/1000)) / TCON4_DIVIDER_VALUE) / (TCON4_PRESCALER_VALUE + 1);
        printk("DEBUG: timer count %d\n", TCNTB4);

        TCON = (TCON_4_AUTO | TCON_4_UPDATE | (0 << 20));

	elfin_timer_irq.handler = elfin_timer_interrupt;

        printk("Timer Initialized.. IRQ=%d\n", IRQ_TIMER4);

        setup_irq(IRQ_TIMER4, &elfin_timer_irq);

        TCON = (TCON_4_AUTO | (0 << 21) | TCON_4_ONOFF);
}

struct sys_timer elfin_timer = {
	.init		= elfin_time_init,
	.offset		= elfin_gettimeoffset,
};

EXPORT_SYMBOL(elfin_get_rtc_time);
EXPORT_SYMBOL(elfin_set_rtc);

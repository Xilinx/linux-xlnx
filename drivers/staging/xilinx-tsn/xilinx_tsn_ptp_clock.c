// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx FPGA Xilinx TSN PTP protocol clock Controller module.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Syed S <syeds@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/platform_device.h>
#include <linux/of_irq.h>
#include "xilinx_tsn_timer.h"

struct xlnx_ptp_timer {
	struct                 device *dev;
	void __iomem          *baseaddr;
	struct ptp_clock      *ptp_clock;
	struct ptp_clock_info  ptp_clock_info;
	spinlock_t             reg_lock; /* ptp timer lock */
	int                    irq;
	int                    pps_enable;
	int                    countpulse;
	u32                    rtc_value;
};

static void xlnx_tod_read(struct xlnx_ptp_timer *timer, struct timespec64 *ts)
{
	u32 sec, nsec;

	nsec = readl(timer->baseaddr + XTIMER1588_CURRENT_RTC_NS);
	sec = readl(timer->baseaddr + XTIMER1588_CURRENT_RTC_SEC_L);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void xlnx_rtc_offset_write(struct xlnx_ptp_timer *timer,
				  const struct timespec64 *ts)
{
	pr_debug("%s: sec: %lld nsec: %ld\n", __func__, ts->tv_sec, ts->tv_nsec);

	writel(0, (timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_H));
	writel(ts->tv_sec, (timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_L));
	writel(ts->tv_nsec, (timer->baseaddr + XTIMER1588_RTC_OFFSET_NS));
}

static void xlnx_rtc_offset_read(struct xlnx_ptp_timer *timer,
				 struct timespec64 *ts)
{
	ts->tv_sec = readl(timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_L);
	ts->tv_nsec = readl(timer->baseaddr + XTIMER1588_RTC_OFFSET_NS);
}

/* PTP clock operations
 */
static int xlnx_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);

	u32 incval;

	incval = adjust_by_scaled_ppm(timer->rtc_value, scaled_ppm);
	writel(incval, (timer->baseaddr + XTIMER1588_RTC_INCREMENT));
	return 0;
}

static int xlnx_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	unsigned long flags;
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);
	struct timespec64 now, then = ns_to_timespec64(delta);

	spin_lock_irqsave(&timer->reg_lock, flags);

	xlnx_rtc_offset_read(timer, &now);

	now = timespec64_add(now, then);

	xlnx_rtc_offset_write(timer, (const struct timespec64 *)&now);
	spin_unlock_irqrestore(&timer->reg_lock, flags);

	return 0;
}

static int xlnx_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	unsigned long flags;
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);
	spin_lock_irqsave(&timer->reg_lock, flags);

	xlnx_tod_read(timer, ts);

	spin_unlock_irqrestore(&timer->reg_lock, flags);
	return 0;
}

/**
 * xlnx_ptp_settime - Set the current time on the hardware clock
 * @ptp: ptp clock structure
 * @ts: timespec64 containing the new time for the cycle counter
 *
 * Return: 0 in all cases.
 *
 * The seconds register is written first, then the nanosecond
 * The hardware loads the entire new value when a nanosecond register
 * is written
 **/
static int xlnx_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);
	struct timespec64 delta, tod;
	struct timespec64 offset;
	unsigned long flags;

	spin_lock_irqsave(&timer->reg_lock, flags);

	/* First zero the offset */
	offset.tv_sec = 0;
	offset.tv_nsec = 0;
	xlnx_rtc_offset_write(timer, &offset);

	/* Get the current timer value */
	xlnx_tod_read(timer, &tod);

	/* Subtract the current reported time from our desired time */
	delta = timespec64_sub(*ts, tod);

	/* Don't write a negative offset */
	if (delta.tv_sec <= 0) {
		delta.tv_sec = 0;
		if (delta.tv_nsec < 0)
			delta.tv_nsec = 0;
	}

	xlnx_rtc_offset_write(timer, &delta);
	spin_unlock_irqrestore(&timer->reg_lock, flags);
	return 0;
}

static int xlnx_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);

	switch (rq->type) {
	case PTP_CLK_REQ_PPS:
		timer->pps_enable = 1;
		return 0;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static struct ptp_clock_info xlnx_ptp_clock_info = {
	.owner    = THIS_MODULE,
	.name     = "Xilinx Timer",
	.max_adj  = 999999999,
	.n_ext_ts	= 0,
	.pps      = 1,
	.adjfine  = xlnx_ptp_adjfine,
	.adjtime  = xlnx_ptp_adjtime,
	.gettime64  = xlnx_ptp_gettime,
	.settime64 = xlnx_ptp_settime,
	.enable   = xlnx_ptp_enable,
};

/* module operations */

/**
 * xlnx_ptp_timer_isr - Interrupt Service Routine
 * @irq:               IRQ number
 * @priv:              pointer to the timer structure
 *
 * Returns: IRQ_HANDLED for all cases
 *
 * Handles the timer interrupt. The timer interrupt fires 128 times per
 * secound. When our count reaches 128 emit a PTP_CLOCK_PPS event
 */
static irqreturn_t xlnx_ptp_timer_isr(int irq, void *priv)
{
	struct xlnx_ptp_timer *timer = (struct xlnx_ptp_timer *)priv;
	struct ptp_clock_event event;

	event.type = PTP_CLOCK_PPS;
	++timer->countpulse;
	if (timer->countpulse >= PULSESIN1PPS) {
		timer->countpulse = 0;
		if (timer->ptp_clock && timer->pps_enable)
			ptp_clock_event(timer->ptp_clock, &event);
	}
	writel((1 << XTIMER1588_INT_SHIFT),
	       (timer->baseaddr + XTIMER1588_INTERRUPT));

	return IRQ_HANDLED;
}

int axienet_get_phc_index(void *priv)
{
	struct xlnx_ptp_timer *timer = (struct xlnx_ptp_timer *)priv;

	if (timer->ptp_clock)
		return ptp_clock_index(timer->ptp_clock);
	else
		return -1;
}

static void tsn_ptp_unregister(void *ptp)
{
	ptp_clock_unregister((struct ptp_clock *)ptp);
}

void *axienet_ptp_timer_probe(void __iomem *base, struct platform_device *pdev)
{
	struct xlnx_ptp_timer *timer;
	struct timespec64 ts;
	int err = 0;

	timer = devm_kzalloc(&pdev->dev, sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return ERR_PTR(-ENOMEM);

	timer->baseaddr = base;

	timer->irq = platform_get_irq_byname(pdev, "interrupt_ptp_timer");

	if (timer->irq < 0) {
		timer->irq = platform_get_irq_byname(pdev, "rtc_irq");
		if (timer->irq > 0) {
			pr_err("ptp timer interrupt name 'rtc_irq' is deprecated\n");
		} else {
			pr_err("ptp timer interrupt not found\n");
			return ERR_PTR(-EINVAL);
		}
	}
	spin_lock_init(&timer->reg_lock);

	timer->ptp_clock_info = xlnx_ptp_clock_info;

	timer->ptp_clock = ptp_clock_register(&timer->ptp_clock_info,
					      &pdev->dev);
	if (IS_ERR(timer->ptp_clock)) {
		err = PTR_ERR(timer->ptp_clock);
		pr_debug("Failed to register ptp clock\n");
		goto out;
	}
	err = devm_add_action_or_reset(&pdev->dev,
				       tsn_ptp_unregister,
				       timer->ptp_clock);
	if (err) {
		pr_debug("Failed to add PTP clock unregister action\n");
		goto out;
	}

	axienet_phc_index = ptp_clock_index(timer->ptp_clock);

	ts = ktime_to_timespec64(ktime_get_real());

	xlnx_ptp_settime(&timer->ptp_clock_info, &ts);
	/* In the TSN IP Core, RTC clock is connected to gtx_clk which is
	 * 125 MHz. This is specified in the TSN PG and is not configurable.
	 *
	 * Calculating the RTC Increment Value once and storing it in
	 * timer->rtc_value to prevent recalculating it each time the PTP
	 * frequency is adjusted in xlnx_ptp_adjfine()
	 */
	timer->rtc_value = (div_u64(NSEC_PER_SEC, XTIMER1588_GTX_CLK_FREQ) <<
			    XTIMER1588_RTC_NS_SHIFT);
	writel(timer->rtc_value, (timer->baseaddr + XTIMER1588_RTC_INCREMENT));

	/* Enable interrupts */
	err = devm_request_irq(&pdev->dev, timer->irq,
			       xlnx_ptp_timer_isr,
			       0,
			       "ptp_rtc",
			       (void *)timer);
	if (err) {
		pr_err("Failed to request IRQ: %d\n", err);
		ptp_clock_unregister(timer->ptp_clock);
		goto out;
	}

	return timer;
out:
	timer->ptp_clock = NULL;
	return ERR_PTR(err);
}

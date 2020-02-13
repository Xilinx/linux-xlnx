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
};

static void xlnx_tod_read(struct xlnx_ptp_timer *timer, struct timespec64 *ts)
{
	u32 sec, nsec;

	nsec = in_be32(timer->baseaddr + XTIMER1588_CURRENT_RTC_NS);
	sec = in_be32(timer->baseaddr + XTIMER1588_CURRENT_RTC_SEC_L);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void xlnx_rtc_offset_write(struct xlnx_ptp_timer *timer,
				  const struct timespec64 *ts)
{
	pr_debug("%s: sec: %ld nsec: %ld\n", __func__, ts->tv_sec, ts->tv_nsec);

	out_be32((timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_H), 0);
	out_be32((timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_L),
		 (ts->tv_sec));
	out_be32((timer->baseaddr + XTIMER1588_RTC_OFFSET_NS), ts->tv_nsec);
}

static void xlnx_rtc_offset_read(struct xlnx_ptp_timer *timer,
				 struct timespec64 *ts)
{
	ts->tv_sec = in_be32(timer->baseaddr + XTIMER1588_RTC_OFFSET_SEC_L);
	ts->tv_nsec = in_be32(timer->baseaddr + XTIMER1588_RTC_OFFSET_NS);
}

/* PTP clock operations
 */
static int xlnx_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct xlnx_ptp_timer *timer = container_of(ptp, struct xlnx_ptp_timer,
						    ptp_clock_info);

	int neg_adj = 0;
	u64 freq;
	u32 diff, incval;

	/* This number should be replaced by a call to get the frequency
	 * from the device-tree. Currently assumes 125MHz
	 */
	incval = 0x800000;
	/* for 156.25 MHZ Ref clk the value is  incval = 0x800000; */

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	freq = incval;
	freq *= ppb;
	diff = div_u64(freq, 1000000000ULL);

	pr_debug("%s: adj: %d ppb: %d\n", __func__, diff, ppb);

	incval = neg_adj ? (incval - diff) : (incval + diff);
	out_be32((timer->baseaddr + XTIMER1588_RTC_INCREMENT), incval);
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
	.adjfreq  = xlnx_ptp_adjfreq,
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
		if ((timer->ptp_clock) && (timer->pps_enable))
			ptp_clock_event(timer->ptp_clock, &event);
	}
	out_be32((timer->baseaddr + XTIMER1588_INTERRUPT),
		 (1 << XTIMER1588_INT_SHIFT));

	return IRQ_HANDLED;
}

int axienet_ptp_timer_remove(void *priv)
{
	struct xlnx_ptp_timer *timer = (struct xlnx_ptp_timer *)priv;

	free_irq(timer->irq, (void *)timer);

	axienet_phc_index = -1;
	if (timer->ptp_clock) {
		ptp_clock_unregister(timer->ptp_clock);
		timer->ptp_clock = NULL;
	}
	kfree(timer);
	return 0;
}

int axienet_get_phc_index(void *priv)
{
	struct xlnx_ptp_timer *timer = (struct xlnx_ptp_timer *)priv;

	if (timer->ptp_clock)
		return ptp_clock_index(timer->ptp_clock);
	else
		return -1;
}

void *axienet_ptp_timer_probe(void __iomem *base, struct platform_device *pdev)
{
	struct xlnx_ptp_timer *timer;
	struct timespec64 ts;
	int err = 0;

	timer = kzalloc(sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return NULL;

	timer->baseaddr = base;

	timer->irq = platform_get_irq_byname(pdev, "interrupt_ptp_timer");

	if (timer->irq < 0) {
		timer->irq = platform_get_irq_byname(pdev, "rtc_irq");
		if (timer->irq > 0) {
			pr_err("ptp timer interrupt name 'rtc_irq' is"
				"deprecated\n");
		} else {
			pr_err("ptp timer interrupt not found\n");
			kfree(timer);
			return NULL;
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

	axienet_phc_index = ptp_clock_index(timer->ptp_clock);

	ts = ktime_to_timespec64(ktime_get_real());

	xlnx_ptp_settime(&timer->ptp_clock_info, &ts);

	/* Enable interrupts */
	err = request_irq(timer->irq,
			  xlnx_ptp_timer_isr,
			  0,
			  "ptp_rtc",
			  (void *)timer);
	if (err)
		goto err_irq;

	return timer;

err_irq:
	ptp_clock_unregister(timer->ptp_clock);
out:
	timer->ptp_clock = NULL;
	return NULL;
}

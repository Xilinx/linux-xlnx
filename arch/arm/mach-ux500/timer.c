/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson
 */
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/clksrc-dbx500-prcmu.h>
#include <linux/clocksource.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_data/clocksource-nomadik-mtu.h>

#include <asm/smp_twd.h>

#include "setup.h"
#include "irqs.h"

#include "db8500-regs.h"
#include "id.h"

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(u8500_twd_local_timer,
			      U8500_TWD_BASE, IRQ_LOCALTIMER);

static void __init ux500_twd_init(void)
{
	struct twd_local_timer *twd_local_timer;
	int err;

	/* Use this to switch local timer base if changed in new ASICs */
	twd_local_timer = &u8500_twd_local_timer;

	if (of_have_populated_dt())
		clocksource_of_init();
	else {
		err = twd_local_timer_register(twd_local_timer);
		if (err)
			pr_err("twd_local_timer_register failed %d\n", err);
	}
}
#else
#define ux500_twd_init()	do { } while(0)
#endif

const static struct of_device_id prcmu_timer_of_match[] __initconst = {
	{ .compatible = "stericsson,db8500-prcmu-timer-4", },
	{ },
};

void __init ux500_timer_init(void)
{
	void __iomem *mtu_timer_base;
	void __iomem *prcmu_timer_base;
	void __iomem *tmp_base;
	struct device_node *np;

	if (cpu_is_u8500_family() || cpu_is_ux540_family()) {
		mtu_timer_base = __io_address(U8500_MTU0_BASE);
		prcmu_timer_base = __io_address(U8500_PRCMU_TIMER_4_BASE);
	} else {
		ux500_unknown_soc();
	}

	/* TODO: Once MTU has been DT:ed place code above into else. */
	if (of_have_populated_dt()) {
#ifdef CONFIG_OF
		np = of_find_matching_node(NULL, prcmu_timer_of_match);
		if (!np)
#endif
			goto dt_fail;

		tmp_base = of_iomap(np, 0);
		if (!tmp_base)
			goto dt_fail;

		prcmu_timer_base = tmp_base;
	}

dt_fail:
	/* Doing it the old fashioned way. */

	/*
	 * Here we register the timerblocks active in the system.
	 * Localtimers (twd) is started when both cpu is up and running.
	 * MTU register a clocksource, clockevent and sched_clock.
	 * Since the MTU is located in the VAPE power domain
	 * it will be cleared in sleep which makes it unsuitable.
	 * We however need it as a timer tick (clockevent)
	 * during boot to calibrate delay until twd is started.
	 * RTC-RTT have problems as timer tick during boot since it is
	 * depending on delay which is not yet calibrated. RTC-RTT is in the
	 * always-on powerdomain and is used as clockevent instead of twd when
	 * sleeping.
	 * The PRCMU timer 4 register a clocksource and
	 * sched_clock with higher rating then MTU since is always-on.
	 *
	 */
	if (!of_have_populated_dt())
		nmdk_timer_init(mtu_timer_base, IRQ_MTU0);
	clksrc_dbx500_prcmu_init(prcmu_timer_base);
	ux500_twd_init();
}

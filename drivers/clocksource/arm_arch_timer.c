/*
 *  linux/drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"arm_arch_timer: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>
#include <linux/acpi.h>

#include <asm/arch_timer.h>
#include <asm/virt.h>

#include <clocksource/arm_arch_timer.h>

#define CNTTIDR		0x08
#define CNTTIDR_VIRT(n)	(BIT(1) << ((n) * 4))

#define CNTACR(n)	(0x40 + ((n) * 4))
#define CNTACR_RPCT	BIT(0)
#define CNTACR_RVCT	BIT(1)
#define CNTACR_RFRQ	BIT(2)
#define CNTACR_RVOFF	BIT(3)
#define CNTACR_RWVT	BIT(4)
#define CNTACR_RWPT	BIT(5)

#define CNTVCT_LO	0x08
#define CNTVCT_HI	0x0c
#define CNTFRQ		0x10
#define CNTP_TVAL	0x28
#define CNTP_CTL	0x2c
#define CNTV_TVAL	0x38
#define CNTV_CTL	0x3c

#define ARCH_CP15_TIMER	BIT(0)
#define ARCH_MEM_TIMER	BIT(1)
static unsigned arch_timers_present __initdata;

static void __iomem *arch_counter_base;

struct arch_timer {
	void __iomem *base;
	struct clock_event_device evt;
};

#define to_arch_timer(e) container_of(e, struct arch_timer, evt)

static u32 arch_timer_rate;

enum ppi_nr {
	PHYS_SECURE_PPI,
	PHYS_NONSECURE_PPI,
	VIRT_PPI,
	HYP_PPI,
	MAX_TIMER_PPI
};

static int arch_timer_ppi[MAX_TIMER_PPI];

static struct clock_event_device __percpu *arch_timer_evt;

static enum ppi_nr arch_timer_uses_ppi = VIRT_PPI;
static bool arch_timer_c3stop;
static bool arch_timer_mem_use_virtual;

static bool evtstrm_enable = IS_ENABLED(CONFIG_ARM_ARCH_TIMER_EVTSTREAM);

static int __init early_evtstrm_cfg(char *buf)
{
	return strtobool(buf, &evtstrm_enable);
}
early_param("clocksource.arm_arch_timer.evtstrm", early_evtstrm_cfg);

/*
 * Architected system timer support.
 */

#ifdef CONFIG_FSL_ERRATUM_A008585
DEFINE_STATIC_KEY_FALSE(arch_timer_read_ool_enabled);
EXPORT_SYMBOL_GPL(arch_timer_read_ool_enabled);

static int fsl_a008585_enable = -1;

static int __init early_fsl_a008585_cfg(char *buf)
{
	int ret;
	bool val;

	ret = strtobool(buf, &val);
	if (ret)
		return ret;

	fsl_a008585_enable = val;
	return 0;
}
early_param("clocksource.arm_arch_timer.fsl-a008585", early_fsl_a008585_cfg);

u32 __fsl_a008585_read_cntp_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntp_tval_el0);
}

u32 __fsl_a008585_read_cntv_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntv_tval_el0);
}

u64 __fsl_a008585_read_cntvct_el0(void)
{
	return __fsl_a008585_read_reg(cntvct_el0);
}
EXPORT_SYMBOL(__fsl_a008585_read_cntvct_el0);
#endif /* CONFIG_FSL_ERRATUM_A008585 */

static __always_inline
void arch_timer_reg_write(int access, enum arch_timer_reg reg, u32 val,
			  struct clock_event_device *clk)
{
	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTV_TVAL);
			break;
		}
	} else {
		arch_timer_reg_write_cp15(access, reg, val);
	}
}

static __always_inline
u32 arch_timer_reg_read(int access, enum arch_timer_reg reg,
			struct clock_event_device *clk)
{
	u32 val;

	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTV_TVAL);
			break;
		}
	} else {
		val = arch_timer_reg_read_cp15(access, reg);
	}

	return val;
}

static __always_inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, evt);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, evt);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_VIRT_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_virt_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_VIRT_ACCESS, evt);
}

static __always_inline int timer_shutdown(const int access,
					  struct clock_event_device *clk)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);

	return 0;
}

static int arch_timer_shutdown_virt(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_PHYS_ACCESS, clk);
}

static int arch_timer_shutdown_virt_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_PHYS_ACCESS, clk);
}

static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;
	arch_timer_reg_write(access, ARCH_TIMER_REG_TVAL, evt, clk);
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

#ifdef CONFIG_FSL_ERRATUM_A008585
static __always_inline void fsl_a008585_set_next_event(const int access,
		unsigned long evt, struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cval = evt + arch_counter_get_cntvct();

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS)
		write_sysreg(cval, cntp_cval_el0);
	else if (access == ARCH_TIMER_VIRT_ACCESS)
		write_sysreg(cval, cntv_cval_el0);

	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

static int fsl_a008585_set_next_event_virt(unsigned long evt,
					   struct clock_event_device *clk)
{
	fsl_a008585_set_next_event(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int fsl_a008585_set_next_event_phys(unsigned long evt,
					   struct clock_event_device *clk)
{
	fsl_a008585_set_next_event(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}
#endif /* CONFIG_FSL_ERRATUM_A008585 */

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_virt_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_PHYS_ACCESS, evt, clk);
	return 0;
}

static void fsl_a008585_set_sne(struct clock_event_device *clk)
{
#ifdef CONFIG_FSL_ERRATUM_A008585
	if (!static_branch_unlikely(&arch_timer_read_ool_enabled))
		return;

	if (arch_timer_uses_ppi == VIRT_PPI)
		clk->set_next_event = fsl_a008585_set_next_event_virt;
	else
		clk->set_next_event = fsl_a008585_set_next_event_phys;
#endif
}

static void __arch_timer_setup(unsigned type,
			       struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT;

	if (type == ARCH_CP15_TIMER) {
		if (arch_timer_c3stop)
			clk->features |= CLOCK_EVT_FEAT_C3STOP;
		clk->name = "arch_sys_timer";
		clk->rating = 450;
		clk->cpumask = cpumask_of(smp_processor_id());
		clk->irq = arch_timer_ppi[arch_timer_uses_ppi];
		switch (arch_timer_uses_ppi) {
		case VIRT_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_virt;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt;
			clk->set_next_event = arch_timer_set_next_event_virt;
			break;
		case PHYS_SECURE_PPI:
		case PHYS_NONSECURE_PPI:
		case HYP_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_phys;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys;
			clk->set_next_event = arch_timer_set_next_event_phys;
			break;
		default:
			BUG();
		}

		fsl_a008585_set_sne(clk);
	} else {
		clk->features |= CLOCK_EVT_FEAT_DYNIRQ;
		clk->name = "arch_mem_timer";
		clk->rating = 400;
		clk->cpumask = cpu_all_mask;
		if (arch_timer_mem_use_virtual) {
			clk->set_state_shutdown = arch_timer_shutdown_virt_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt_mem;
			clk->set_next_event =
				arch_timer_set_next_event_virt_mem;
		} else {
			clk->set_state_shutdown = arch_timer_shutdown_phys_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys_mem;
			clk->set_next_event =
				arch_timer_set_next_event_phys_mem;
		}
	}

	clk->set_state_shutdown(clk);

	clockevents_config_and_register(clk, arch_timer_rate, 0xf, 0x7fffffff);
}

static void arch_timer_evtstrm_enable(int divider)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	cntkctl &= ~ARCH_TIMER_EVT_TRIGGER_MASK;
	/* Set the divider and enable virtual event stream */
	cntkctl |= (divider << ARCH_TIMER_EVT_TRIGGER_SHIFT)
			| ARCH_TIMER_VIRT_EVT_EN;
	arch_timer_set_cntkctl(cntkctl);
	elf_hwcap |= HWCAP_EVTSTRM;
#ifdef CONFIG_COMPAT
	compat_elf_hwcap |= COMPAT_HWCAP_EVTSTRM;
#endif
}

static void arch_timer_configure_evtstream(void)
{
	int evt_stream_div, pos;

	/* Find the closest power of two to the divisor */
	evt_stream_div = arch_timer_rate / ARCH_TIMER_EVT_STREAM_FREQ;
	pos = fls(evt_stream_div);
	if (pos > 1 && !(evt_stream_div & (1 << (pos - 2))))
		pos--;
	/* enable event stream */
	arch_timer_evtstrm_enable(min(pos, 15));
}

static void arch_counter_set_user_access(void)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	/* Disable user access to the timers and the physical counter */
	/* Also disable virtual event stream */
	cntkctl &= ~(ARCH_TIMER_USR_PT_ACCESS_EN
			| ARCH_TIMER_USR_VT_ACCESS_EN
			| ARCH_TIMER_VIRT_EVT_EN
			| ARCH_TIMER_USR_PCT_ACCESS_EN);

	/* Enable user access to the virtual counter */
	cntkctl |= ARCH_TIMER_USR_VCT_ACCESS_EN;

	arch_timer_set_cntkctl(cntkctl);
}

static bool arch_timer_has_nonsecure_ppi(void)
{
	return (arch_timer_uses_ppi == PHYS_SECURE_PPI &&
		arch_timer_ppi[PHYS_NONSECURE_PPI]);
}

static u32 check_ppi_trigger(int irq)
{
	u32 flags = irq_get_trigger_type(irq);

	if (flags != IRQF_TRIGGER_HIGH && flags != IRQF_TRIGGER_LOW) {
		pr_warn("WARNING: Invalid trigger for IRQ%d, assuming level low\n", irq);
		pr_warn("WARNING: Please fix your firmware\n");
		flags = IRQF_TRIGGER_LOW;
	}

	return flags;
}

static int arch_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);
	u32 flags;

	__arch_timer_setup(ARCH_CP15_TIMER, clk);

	flags = check_ppi_trigger(arch_timer_ppi[arch_timer_uses_ppi]);
	enable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], flags);

	if (arch_timer_has_nonsecure_ppi()) {
		flags = check_ppi_trigger(arch_timer_ppi[PHYS_NONSECURE_PPI]);
		enable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI], flags);
	}

	arch_counter_set_user_access();
	if (evtstrm_enable)
		arch_timer_configure_evtstream();

	return 0;
}

static void
arch_timer_detect_rate(void __iomem *cntbase, struct device_node *np)
{
	/* Who has more than one independent system counter? */
	if (arch_timer_rate)
		return;

	/*
	 * Try to determine the frequency from the device tree or CNTFRQ,
	 * if ACPI is enabled, get the frequency from CNTFRQ ONLY.
	 */
	if (!acpi_disabled ||
	    of_property_read_u32(np, "clock-frequency", &arch_timer_rate)) {
		if (cntbase)
			arch_timer_rate = readl_relaxed(cntbase + CNTFRQ);
		else
			arch_timer_rate = arch_timer_get_cntfrq();
	}

	/* Check the timer frequency. */
	if (arch_timer_rate == 0)
		pr_warn("Architected timer frequency not available\n");
}

static void arch_timer_banner(unsigned type)
{
	pr_info("Architected %s%s%s timer(s) running at %lu.%02luMHz (%s%s%s).\n",
		     type & ARCH_CP15_TIMER ? "cp15" : "",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  " and " : "",
		     type & ARCH_MEM_TIMER ? "mmio" : "",
		     (unsigned long)arch_timer_rate / 1000000,
		     (unsigned long)(arch_timer_rate / 10000) % 100,
		     type & ARCH_CP15_TIMER ?
		     (arch_timer_uses_ppi == VIRT_PPI) ? "virt" : "phys" :
			"",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  "/" : "",
		     type & ARCH_MEM_TIMER ?
			arch_timer_mem_use_virtual ? "virt" : "phys" :
			"");
}

u32 arch_timer_get_rate(void)
{
	return arch_timer_rate;
}

static u64 arch_counter_get_cntvct_mem(void)
{
	u32 vct_lo, vct_hi, tmp_hi;

	do {
		vct_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
		vct_lo = readl_relaxed(arch_counter_base + CNTVCT_LO);
		tmp_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
	} while (vct_hi != tmp_hi);

	return ((u64) vct_hi << 32) | vct_lo;
}

/*
 * Default to cp15 based access because arm64 uses this function for
 * sched_clock() before DT is probed and the cp15 method is guaranteed
 * to exist on arm64. arm doesn't use this before DT is probed so even
 * if we don't have the cp15 accessors we won't have a problem.
 */
u64 (*arch_timer_read_counter)(void) = arch_counter_get_cntvct;

static cycle_t arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}

static cycle_t arch_counter_read_cc(const struct cyclecounter *cc)
{
	return arch_timer_read_counter();
}

static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.rating	= 400,
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP,
};

static struct cyclecounter cyclecounter = {
	.read	= arch_counter_read_cc,
	.mask	= CLOCKSOURCE_MASK(56),
};

static struct arch_timer_kvm_info arch_timer_kvm_info;

struct arch_timer_kvm_info *arch_timer_get_kvm_info(void)
{
	return &arch_timer_kvm_info;
}

static void __init arch_counter_register(unsigned type)
{
	u64 start_count;

	/* Register the CP15 based counter if we have one */
	if (type & ARCH_CP15_TIMER) {
		if (IS_ENABLED(CONFIG_ARM64) || arch_timer_uses_ppi == VIRT_PPI)
			arch_timer_read_counter = arch_counter_get_cntvct;
		else
			arch_timer_read_counter = arch_counter_get_cntpct;

		clocksource_counter.archdata.vdso_direct = true;

#ifdef CONFIG_FSL_ERRATUM_A008585
		/*
		 * Don't use the vdso fastpath if errata require using
		 * the out-of-line counter accessor.
		 */
		if (static_branch_unlikely(&arch_timer_read_ool_enabled))
			clocksource_counter.archdata.vdso_direct = false;
#endif
	} else {
		arch_timer_read_counter = arch_counter_get_cntvct_mem;
	}

	start_count = arch_timer_read_counter();
	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&arch_timer_kvm_info.timecounter,
			 &cyclecounter, start_count);

	/* 56 bits minimum, so we assume worst case rollover */
	sched_clock_register(arch_timer_read_counter, 56, arch_timer_rate);
}

static void arch_timer_stop(struct clock_event_device *clk)
{
	pr_debug("arch_timer_teardown disable IRQ%d cpu #%d\n",
		 clk->irq, smp_processor_id());

	disable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi]);
	if (arch_timer_has_nonsecure_ppi())
		disable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI]);

	clk->set_state_shutdown(clk);
}

static int arch_timer_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);

	arch_timer_stop(clk);
	return 0;
}

#ifdef CONFIG_CPU_PM
static unsigned int saved_cntkctl;
static int arch_timer_cpu_pm_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	if (action == CPU_PM_ENTER)
		saved_cntkctl = arch_timer_get_cntkctl();
	else if (action == CPU_PM_ENTER_FAILED || action == CPU_PM_EXIT)
		arch_timer_set_cntkctl(saved_cntkctl);
	return NOTIFY_OK;
}

static struct notifier_block arch_timer_cpu_pm_notifier = {
	.notifier_call = arch_timer_cpu_pm_notify,
};

static int __init arch_timer_cpu_pm_init(void)
{
	return cpu_pm_register_notifier(&arch_timer_cpu_pm_notifier);
}

static void __init arch_timer_cpu_pm_deinit(void)
{
	WARN_ON(cpu_pm_unregister_notifier(&arch_timer_cpu_pm_notifier));
}

#else
static int __init arch_timer_cpu_pm_init(void)
{
	return 0;
}

static void __init arch_timer_cpu_pm_deinit(void)
{
}
#endif

static int __init arch_timer_register(void)
{
	int err;
	int ppi;

	arch_timer_evt = alloc_percpu(struct clock_event_device);
	if (!arch_timer_evt) {
		err = -ENOMEM;
		goto out;
	}

	ppi = arch_timer_ppi[arch_timer_uses_ppi];
	switch (arch_timer_uses_ppi) {
	case VIRT_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
		break;
	case PHYS_SECURE_PPI:
	case PHYS_NONSECURE_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		if (!err && arch_timer_ppi[PHYS_NONSECURE_PPI]) {
			ppi = arch_timer_ppi[PHYS_NONSECURE_PPI];
			err = request_percpu_irq(ppi, arch_timer_handler_phys,
						 "arch_timer", arch_timer_evt);
			if (err)
				free_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI],
						arch_timer_evt);
		}
		break;
	case HYP_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		break;
	default:
		BUG();
	}

	if (err) {
		pr_err("arch_timer: can't register interrupt %d (%d)\n",
		       ppi, err);
		goto out_free;
	}

	err = arch_timer_cpu_pm_init();
	if (err)
		goto out_unreg_notify;


	/* Register and immediately configure the timer on the boot CPU */
	err = cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_STARTING,
				"AP_ARM_ARCH_TIMER_STARTING",
				arch_timer_starting_cpu, arch_timer_dying_cpu);
	if (err)
		goto out_unreg_cpupm;
	return 0;

out_unreg_cpupm:
	arch_timer_cpu_pm_deinit();

out_unreg_notify:
	free_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], arch_timer_evt);
	if (arch_timer_has_nonsecure_ppi())
		free_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI],
				arch_timer_evt);

out_free:
	free_percpu(arch_timer_evt);
out:
	return err;
}

static int __init arch_timer_mem_register(void __iomem *base, unsigned int irq)
{
	int ret;
	irq_handler_t func;
	struct arch_timer *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->base = base;
	t->evt.irq = irq;
	__arch_timer_setup(ARCH_MEM_TIMER, &t->evt);

	if (arch_timer_mem_use_virtual)
		func = arch_timer_handler_virt_mem;
	else
		func = arch_timer_handler_phys_mem;

	ret = request_irq(irq, func, IRQF_TIMER, "arch_mem_timer", &t->evt);
	if (ret) {
		pr_err("arch_timer: Failed to request mem timer irq\n");
		kfree(t);
	}

	return ret;
}

static const struct of_device_id arch_timer_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer",    },
	{ .compatible   = "arm,armv8-timer",    },
	{},
};

static const struct of_device_id arch_timer_mem_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer-mem", },
	{},
};

static bool __init
arch_timer_needs_probing(int type, const struct of_device_id *matches)
{
	struct device_node *dn;
	bool needs_probing = false;

	dn = of_find_matching_node(NULL, matches);
	if (dn && of_device_is_available(dn) && !(arch_timers_present & type))
		needs_probing = true;
	of_node_put(dn);

	return needs_probing;
}

static int __init arch_timer_common_init(void)
{
	unsigned mask = ARCH_CP15_TIMER | ARCH_MEM_TIMER;

	/* Wait until both nodes are probed if we have two timers */
	if ((arch_timers_present & mask) != mask) {
		if (arch_timer_needs_probing(ARCH_MEM_TIMER, arch_timer_mem_of_match))
			return 0;
		if (arch_timer_needs_probing(ARCH_CP15_TIMER, arch_timer_of_match))
			return 0;
	}

	arch_timer_banner(arch_timers_present);
	arch_counter_register(arch_timers_present);
	return arch_timer_arch_init();
}

static int __init arch_timer_init(void)
{
	int ret;
	/*
	 * If HYP mode is available, we know that the physical timer
	 * has been configured to be accessible from PL1. Use it, so
	 * that a guest can use the virtual timer instead.
	 *
	 * If no interrupt provided for virtual timer, we'll have to
	 * stick to the physical timer. It'd better be accessible...
	 *
	 * On ARMv8.1 with VH extensions, the kernel runs in HYP. VHE
	 * accesses to CNTP_*_EL1 registers are silently redirected to
	 * their CNTHP_*_EL2 counterparts, and use a different PPI
	 * number.
	 */
	if (is_hyp_mode_available() || !arch_timer_ppi[VIRT_PPI]) {
		bool has_ppi;

		if (is_kernel_in_hyp_mode()) {
			arch_timer_uses_ppi = HYP_PPI;
			has_ppi = !!arch_timer_ppi[HYP_PPI];
		} else {
			arch_timer_uses_ppi = PHYS_SECURE_PPI;
			has_ppi = (!!arch_timer_ppi[PHYS_SECURE_PPI] ||
				   !!arch_timer_ppi[PHYS_NONSECURE_PPI]);
		}

		if (!has_ppi) {
			pr_warn("arch_timer: No interrupt available, giving up\n");
			return -EINVAL;
		}
	}

	ret = arch_timer_register();
	if (ret)
		return ret;

	ret = arch_timer_common_init();
	if (ret)
		return ret;

	arch_timer_kvm_info.virtual_irq = arch_timer_ppi[VIRT_PPI];
	
	return 0;
}

static int __init arch_timer_of_init(struct device_node *np)
{
	int i;

	if (arch_timers_present & ARCH_CP15_TIMER) {
		pr_warn("arch_timer: multiple nodes in dt, skipping\n");
		return 0;
	}

	arch_timers_present |= ARCH_CP15_TIMER;
	for (i = PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++)
		arch_timer_ppi[i] = irq_of_parse_and_map(np, i);

	arch_timer_detect_rate(NULL, np);

	arch_timer_c3stop = !of_property_read_bool(np, "always-on");

#ifdef CONFIG_FSL_ERRATUM_A008585
	if (fsl_a008585_enable < 0)
		fsl_a008585_enable = of_property_read_bool(np, "fsl,erratum-a008585");
	if (fsl_a008585_enable) {
		static_branch_enable(&arch_timer_read_ool_enabled);
		pr_info("Enabling workaround for FSL erratum A-008585\n");
	}
#endif

	/*
	 * If we cannot rely on firmware initializing the timer registers then
	 * we should use the physical timers instead.
	 */
	if (IS_ENABLED(CONFIG_ARM) &&
	    of_property_read_bool(np, "arm,cpu-registers-not-fw-configured"))
		arch_timer_uses_ppi = PHYS_SECURE_PPI;

	return arch_timer_init();
}
CLOCKSOURCE_OF_DECLARE(armv7_arch_timer, "arm,armv7-timer", arch_timer_of_init);
CLOCKSOURCE_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", arch_timer_of_init);

static int __init arch_timer_mem_init(struct device_node *np)
{
	struct device_node *frame, *best_frame = NULL;
	void __iomem *cntctlbase, *base;
	unsigned int irq, ret = -EINVAL;
	u32 cnttidr;

	arch_timers_present |= ARCH_MEM_TIMER;
	cntctlbase = of_iomap(np, 0);
	if (!cntctlbase) {
		pr_err("arch_timer: Can't find CNTCTLBase\n");
		return -ENXIO;
	}

	cnttidr = readl_relaxed(cntctlbase + CNTTIDR);

	/*
	 * Try to find a virtual capable frame. Otherwise fall back to a
	 * physical capable frame.
	 */
	for_each_available_child_of_node(np, frame) {
		int n;
		u32 cntacr;

		if (of_property_read_u32(frame, "frame-number", &n)) {
			pr_err("arch_timer: Missing frame-number\n");
			of_node_put(frame);
			goto out;
		}

		/* Try enabling everything, and see what sticks */
		cntacr = CNTACR_RFRQ | CNTACR_RWPT | CNTACR_RPCT |
			 CNTACR_RWVT | CNTACR_RVOFF | CNTACR_RVCT;
		writel_relaxed(cntacr, cntctlbase + CNTACR(n));
		cntacr = readl_relaxed(cntctlbase + CNTACR(n));

		if ((cnttidr & CNTTIDR_VIRT(n)) &&
		    !(~cntacr & (CNTACR_RWVT | CNTACR_RVCT))) {
			of_node_put(best_frame);
			best_frame = frame;
			arch_timer_mem_use_virtual = true;
			break;
		}

		if (~cntacr & (CNTACR_RWPT | CNTACR_RPCT))
			continue;

		of_node_put(best_frame);
		best_frame = of_node_get(frame);
	}

	ret= -ENXIO;
	base = arch_counter_base = of_iomap(best_frame, 0);
	if (!base) {
		pr_err("arch_timer: Can't map frame's registers\n");
		goto out;
	}

	if (arch_timer_mem_use_virtual)
		irq = irq_of_parse_and_map(best_frame, 1);
	else
		irq = irq_of_parse_and_map(best_frame, 0);

	ret = -EINVAL;
	if (!irq) {
		pr_err("arch_timer: Frame missing %s irq",
		       arch_timer_mem_use_virtual ? "virt" : "phys");
		goto out;
	}

	arch_timer_detect_rate(base, np);
	ret = arch_timer_mem_register(base, irq);
	if (ret)
		goto out;

	return arch_timer_common_init();
out:
	iounmap(cntctlbase);
	of_node_put(best_frame);
	return ret;
}
CLOCKSOURCE_OF_DECLARE(armv7_arch_timer_mem, "arm,armv7-timer-mem",
		       arch_timer_mem_init);

#ifdef CONFIG_ACPI
static int __init map_generic_timer_interrupt(u32 interrupt, u32 flags)
{
	int trigger, polarity;

	if (!interrupt)
		return 0;

	trigger = (flags & ACPI_GTDT_INTERRUPT_MODE) ? ACPI_EDGE_SENSITIVE
			: ACPI_LEVEL_SENSITIVE;

	polarity = (flags & ACPI_GTDT_INTERRUPT_POLARITY) ? ACPI_ACTIVE_LOW
			: ACPI_ACTIVE_HIGH;

	return acpi_register_gsi(NULL, interrupt, trigger, polarity);
}

/* Initialize per-processor generic timer */
static int __init arch_timer_acpi_init(struct acpi_table_header *table)
{
	struct acpi_table_gtdt *gtdt;

	if (arch_timers_present & ARCH_CP15_TIMER) {
		pr_warn("arch_timer: already initialized, skipping\n");
		return -EINVAL;
	}

	gtdt = container_of(table, struct acpi_table_gtdt, header);

	arch_timers_present |= ARCH_CP15_TIMER;

	arch_timer_ppi[PHYS_SECURE_PPI] =
		map_generic_timer_interrupt(gtdt->secure_el1_interrupt,
		gtdt->secure_el1_flags);

	arch_timer_ppi[PHYS_NONSECURE_PPI] =
		map_generic_timer_interrupt(gtdt->non_secure_el1_interrupt,
		gtdt->non_secure_el1_flags);

	arch_timer_ppi[VIRT_PPI] =
		map_generic_timer_interrupt(gtdt->virtual_timer_interrupt,
		gtdt->virtual_timer_flags);

	arch_timer_ppi[HYP_PPI] =
		map_generic_timer_interrupt(gtdt->non_secure_el2_interrupt,
		gtdt->non_secure_el2_flags);

	/* Get the frequency from CNTFRQ */
	arch_timer_detect_rate(NULL, NULL);

	/* Always-on capability */
	arch_timer_c3stop = !(gtdt->non_secure_el1_flags & ACPI_GTDT_ALWAYS_ON);

	arch_timer_init();
	return 0;
}
CLOCKSOURCE_ACPI_DECLARE(arch_timer, ACPI_SIG_GTDT, arch_timer_acpi_init);
#endif

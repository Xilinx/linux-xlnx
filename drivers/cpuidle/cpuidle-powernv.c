/*
 *  cpuidle-powernv - idle state cpuidle driver.
 *  Adapted from drivers/cpuidle/cpuidle-pseries
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/opal.h>
#include <asm/runlatch.h>

#define POWERNV_THRESHOLD_LATENCY_NS 200000

struct cpuidle_driver powernv_idle_driver = {
	.name             = "powernv_idle",
	.owner            = THIS_MODULE,
};

static int max_idle_state;
static struct cpuidle_state *cpuidle_state_table;

static u64 stop_psscr_table[CPUIDLE_STATE_MAX];

static u64 snooze_timeout;
static bool snooze_timeout_en;

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	u64 snooze_exit_time;

	local_irq_enable();
	set_thread_flag(TIF_POLLING_NRFLAG);

	snooze_exit_time = get_tb() + snooze_timeout;
	ppc64_runlatch_off();
	while (!need_resched()) {
		HMT_low();
		HMT_very_low();
		if (snooze_timeout_en && get_tb() > snooze_exit_time)
			break;
	}

	HMT_medium();
	ppc64_runlatch_on();
	clear_thread_flag(TIF_POLLING_NRFLAG);
	smp_mb();
	return index;
}

static int nap_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	ppc64_runlatch_off();
	power7_idle();
	ppc64_runlatch_on();
	return index;
}

/* Register for fastsleep only in oneshot mode of broadcast */
#ifdef CONFIG_TICK_ONESHOT
static int fastsleep_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long old_lpcr = mfspr(SPRN_LPCR);
	unsigned long new_lpcr;

	if (unlikely(system_state < SYSTEM_RUNNING))
		return index;

	new_lpcr = old_lpcr;
	/* Do not exit powersave upon decrementer as we've setup the timer
	 * offload.
	 */
	new_lpcr &= ~LPCR_PECE1;

	mtspr(SPRN_LPCR, new_lpcr);
	power7_sleep();

	mtspr(SPRN_LPCR, old_lpcr);

	return index;
}
#endif

static int stop_loop(struct cpuidle_device *dev,
		     struct cpuidle_driver *drv,
		     int index)
{
	ppc64_runlatch_off();
	power9_idle_stop(stop_psscr_table[index]);
	ppc64_runlatch_on();
	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state powernv_states[CPUIDLE_STATE_MAX] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = snooze_loop },
};

static int powernv_cpuidle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_enable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

static int powernv_cpuidle_cpu_dead(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_disable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

/*
 * powernv_cpuidle_driver_init()
 */
static int powernv_cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &powernv_idle_driver;

	drv->state_count = 0;

	for (idle_state = 0; idle_state < max_idle_state; ++idle_state) {
		/* Is the state not enabled? */
		if (cpuidle_state_table[idle_state].enter == NULL)
			continue;

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[idle_state];

		drv->state_count += 1;
	}

	return 0;
}

static int powernv_add_idle_states(void)
{
	struct device_node *power_mgt;
	int nr_idle_states = 1; /* Snooze */
	int dt_idle_states;
	u32 latency_ns[CPUIDLE_STATE_MAX];
	u32 residency_ns[CPUIDLE_STATE_MAX];
	u32 flags[CPUIDLE_STATE_MAX];
	u64 psscr_val[CPUIDLE_STATE_MAX];
	const char *names[CPUIDLE_STATE_MAX];
	int i, rc;

	/* Currently we have snooze statically defined */

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("opal: PowerMgmt Node not found\n");
		goto out;
	}

	/* Read values of any property to determine the num of idle states */
	dt_idle_states = of_property_count_u32_elems(power_mgt, "ibm,cpu-idle-state-flags");
	if (dt_idle_states < 0) {
		pr_warn("cpuidle-powernv: no idle states found in the DT\n");
		goto out;
	}

	/*
	 * Since snooze is used as first idle state, max idle states allowed is
	 * CPUIDLE_STATE_MAX -1
	 */
	if (dt_idle_states > CPUIDLE_STATE_MAX - 1) {
		pr_warn("cpuidle-powernv: discovered idle states more than allowed");
		dt_idle_states = CPUIDLE_STATE_MAX - 1;
	}

	if (of_property_read_u32_array(power_mgt,
			"ibm,cpu-idle-state-flags", flags, dt_idle_states)) {
		pr_warn("cpuidle-powernv : missing ibm,cpu-idle-state-flags in DT\n");
		goto out;
	}

	if (of_property_read_u32_array(power_mgt,
		"ibm,cpu-idle-state-latencies-ns", latency_ns,
		dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-latencies-ns in DT\n");
		goto out;
	}
	if (of_property_read_string_array(power_mgt,
		"ibm,cpu-idle-state-names", names, dt_idle_states) < 0) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-names in DT\n");
		goto out;
	}

	/*
	 * If the idle states use stop instruction, probe for psscr values
	 * which are necessary to specify required stop level.
	 */
	if (flags[0] & (OPAL_PM_STOP_INST_FAST | OPAL_PM_STOP_INST_DEEP))
		if (of_property_read_u64_array(power_mgt,
		    "ibm,cpu-idle-state-psscr", psscr_val, dt_idle_states)) {
			pr_warn("cpuidle-powernv: missing ibm,cpu-idle-states-psscr in DT\n");
			goto out;
		}

	rc = of_property_read_u32_array(power_mgt,
		"ibm,cpu-idle-state-residency-ns", residency_ns, dt_idle_states);

	for (i = 0; i < dt_idle_states; i++) {
		/*
		 * If an idle state has exit latency beyond
		 * POWERNV_THRESHOLD_LATENCY_NS then don't use it
		 * in cpu-idle.
		 */
		if (latency_ns[i] > POWERNV_THRESHOLD_LATENCY_NS)
			continue;

		/*
		 * Cpuidle accepts exit_latency and target_residency in us.
		 * Use default target_residency values if f/w does not expose it.
		 */
		if (flags[i] & OPAL_PM_NAP_ENABLED) {
			/* Add NAP state */
			strcpy(powernv_states[nr_idle_states].name, "Nap");
			strcpy(powernv_states[nr_idle_states].desc, "Nap");
			powernv_states[nr_idle_states].flags = 0;
			powernv_states[nr_idle_states].target_residency = 100;
			powernv_states[nr_idle_states].enter = nap_loop;
		} else if ((flags[i] & OPAL_PM_STOP_INST_FAST) &&
				!(flags[i] & OPAL_PM_TIMEBASE_STOP)) {
			strncpy(powernv_states[nr_idle_states].name,
				names[i], CPUIDLE_NAME_LEN);
			strncpy(powernv_states[nr_idle_states].desc,
				names[i], CPUIDLE_NAME_LEN);
			powernv_states[nr_idle_states].flags = 0;

			powernv_states[nr_idle_states].enter = stop_loop;
			stop_psscr_table[nr_idle_states] = psscr_val[i];
		}

		/*
		 * All cpuidle states with CPUIDLE_FLAG_TIMER_STOP set must come
		 * within this config dependency check.
		 */
#ifdef CONFIG_TICK_ONESHOT
		if (flags[i] & OPAL_PM_SLEEP_ENABLED ||
			flags[i] & OPAL_PM_SLEEP_ENABLED_ER1) {
			/* Add FASTSLEEP state */
			strcpy(powernv_states[nr_idle_states].name, "FastSleep");
			strcpy(powernv_states[nr_idle_states].desc, "FastSleep");
			powernv_states[nr_idle_states].flags = CPUIDLE_FLAG_TIMER_STOP;
			powernv_states[nr_idle_states].target_residency = 300000;
			powernv_states[nr_idle_states].enter = fastsleep_loop;
		} else if ((flags[i] & OPAL_PM_STOP_INST_DEEP) &&
				(flags[i] & OPAL_PM_TIMEBASE_STOP)) {
			strncpy(powernv_states[nr_idle_states].name,
				names[i], CPUIDLE_NAME_LEN);
			strncpy(powernv_states[nr_idle_states].desc,
				names[i], CPUIDLE_NAME_LEN);

			powernv_states[nr_idle_states].flags = CPUIDLE_FLAG_TIMER_STOP;
			powernv_states[nr_idle_states].enter = stop_loop;
			stop_psscr_table[nr_idle_states] = psscr_val[i];
		}
#endif
		powernv_states[nr_idle_states].exit_latency =
				((unsigned int)latency_ns[i]) / 1000;

		if (!rc) {
			powernv_states[nr_idle_states].target_residency =
				((unsigned int)residency_ns[i]) / 1000;
		}

		nr_idle_states++;
	}
out:
	return nr_idle_states;
}

/*
 * powernv_idle_probe()
 * Choose state table for shared versus dedicated partition
 */
static int powernv_idle_probe(void)
{
	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		return -ENODEV;

	if (firmware_has_feature(FW_FEATURE_OPAL)) {
		cpuidle_state_table = powernv_states;
		/* Device tree can indicate more idle states */
		max_idle_state = powernv_add_idle_states();
		if (max_idle_state > 1) {
			snooze_timeout_en = true;
			snooze_timeout = powernv_states[1].target_residency *
					 tb_ticks_per_usec;
		}
 	} else
 		return -ENODEV;

	return 0;
}

static int __init powernv_processor_idle_init(void)
{
	int retval;

	retval = powernv_idle_probe();
	if (retval)
		return retval;

	powernv_cpuidle_driver_init();
	retval = cpuidle_register(&powernv_idle_driver, NULL);
	if (retval) {
		printk(KERN_DEBUG "Registration of powernv driver failed.\n");
		return retval;
	}

	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "cpuidle/powernv:online",
					   powernv_cpuidle_cpu_online, NULL);
	WARN_ON(retval < 0);
	retval = cpuhp_setup_state_nocalls(CPUHP_CPUIDLE_DEAD,
					   "cpuidle/powernv:dead", NULL,
					   powernv_cpuidle_cpu_dead);
	WARN_ON(retval < 0);
	printk(KERN_DEBUG "powernv_idle_driver registered\n");
	return 0;
}

device_initcall(powernv_processor_idle_init);

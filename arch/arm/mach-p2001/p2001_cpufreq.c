/*
 *  linux/arch/arm/mach-p2001/p2001_cpufreq.c
 *
 *  Copyright (C) 2004-2005 Tobias Lorenz
 *
 *  CPU frequency scaling support
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
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>

static struct cpufreq_driver p2001_cpufreq_driver;

static int p2001_cpufreq_driver_init(struct cpufreq_policy *policy);
static int p2001_cpufreq_driver_verify(struct cpufreq_policy *policy);
//static int p2001_cpufreq_driver_setpolicy(struct cpufreq_policy *policy);
static int p2001_cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);
static unsigned int p2001_cpufreq_driver_get(unsigned int cpu);

static struct cpufreq_frequency_table p2001_cpufreq_frequency_table[] =
{
	/* index is also the scaling factor */
						//       6 kHz (minimum)
	{ .index = 1, .frequency =   12288 },	//  12.288 MHz (no network)
	{ .index = 2, .frequency =   24576 },	//  24.576 MHz (no network)
	{ .index = 3, .frequency =   36864 },	//  36.864 MHz
	{ .index = 4, .frequency =   49152 },	//  49.152 MHz
	{ .index = 5, .frequency =   61440 },	//  61.440 MHz
	{ .index = 6, .frequency =   73728 },	//  73.728 MHz
	{ .index = 7, .frequency =   86016 },	//  86.016 MHz (overclocked)
	{ .index = 8, .frequency =   98304 },	//  98.304 MHz (overclocked)
	{ .index = 9, .frequency =  110592 },	// 110.592 MHz (not working)
	{ .frequency = CPUFREQ_TABLE_END   },
};

static int p2001_cpufreq_driver_init(struct cpufreq_policy *policy)
{
//	printk("p2001_cpufreq_driver_init\n");

	/* set default policy and cpuinfo */
	if (policy->cpu != 0)
		return -EINVAL;

	policy->cur = policy->min = policy->max = p2001_cpufreq_driver_get(policy->cpu);
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.max_freq = 73728;	// kHz
	policy->cpuinfo.min_freq = 36864;	// kHz
	policy->cpuinfo.transition_latency = 1000000; /* 1 ms, assumed */

	return 0;
}

/**
 * p2001_cpufreq_driver_verify - verifies a new CPUFreq policy
 * @policy: new policy
 *
 * Limit must be within this model's frequency range at least one
 * border included.
 */
static int p2001_cpufreq_driver_verify(struct cpufreq_policy *policy)
{
//	printk("p2001_cpufreq_driver_verify\n");
	return cpufreq_frequency_table_verify(policy, p2001_cpufreq_frequency_table);
}

/**
 * p2001_cpufreq_driver_verify - set a new CPUFreq policy
 * @policy: new policy
 *
 * Sets a new CPUFreq policy.
 */
/*
static int p2001_cpufreq_driver_setpolicy(struct cpufreq_policy *policy)
{
//	printk("p2001_cpufreq_driver_setpolicy\n");
	// what to do here ?
	return 0;
}
*/

/**
 * p2001_cpufreq_driver_target - set a new CPUFreq policy
 * @policy: new policy
 * @target_freq: the target frequency
 * @relation: how that frequency relates to achieved frequency (CPUFREQ_RELATION_L or CPUFREQ_RELATION_H)
 *
 * Sets a new CPUFreq policy.
 */
static int p2001_cpufreq_driver_target(struct cpufreq_policy *policy,
				       unsigned int target_freq,
				       unsigned int relation)
{
	unsigned int newstate = 0;
	struct cpufreq_freqs freqs;
	unsigned int M, P, S, N, PWRDN;			// PLL_12288_config
	unsigned int M_DIV, N_DIV, SEL_PLL, SEL_DIV;	// DIV_12288_config
	unsigned int config;

//	printk("p2001_cpufreq_driver_target(target_freq=%d)\n", target_freq);
        if (cpufreq_frequency_table_target(policy, &p2001_cpufreq_frequency_table[0], target_freq, relation, &newstate))
                return -EINVAL;

	freqs.old = p2001_cpufreq_driver_get(policy->cpu);
        freqs.new = p2001_cpufreq_frequency_table[newstate].frequency;
	freqs.cpu = 0; /* p2001_cpufreq.c is UP only driver */

	if (freqs.new == freqs.old)
		return 0;
//	printk("System clock change from %d kHz to %d kHz\n", freqs.old, freqs.new);

	M = 0;
	S = 0;
	P = 0;
	N = 0;
	PWRDN = 0;
	M_DIV = 1;
	N_DIV = newstate;
	SEL_PLL = 1;
	SEL_DIV = 0;
	switch (p2001_cpufreq_frequency_table[newstate].index) {
		case 1:	// 12288 kHz
			PWRDN = 1;
			SEL_PLL = 0;
			break;
		case 2:	// 24576 kHz
			P = 2;
			break;
		case 3:	// 36864 kHz
			M = 1;
			P = 1;
			break;
		case 4:	// 49152 kHz
			M = 0;
			break;
		case 5:	// 61440 kHz
			M = 2;
			break;
		case 6:	// 73728 kHz
			M = 4;
			break;
		case 7:	// 86016 kHz
			M = 6;
			break;
		case 8:	// 98304 kHz
			M = 8;
			break;
		case 9:	// 110592 kHz
			M = 10;
			break;
	}

	/* notifier */
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* change DIV first to bypass PLL before PWRDN */
	config = (M_DIV<<0) | (N_DIV<<8) | (SEL_PLL<<16) | (SEL_DIV<<17);
	P2001_TIMER->DIV_12288_config = config;
	config = (M<<0) | (P<<8) | (S<<14) | (N<<16) | (PWRDN<<26);
	P2001_TIMER->PLL_12288_config = config;

	/* notifier */
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

/*
 * returns current frequency in kHz
 */
static unsigned int p2001_cpufreq_driver_get(unsigned int cpu)
{
	cpumask_t cpus_allowed;
	unsigned int current_freq;
	unsigned int M, P, S, N, PWRDN;			// PLL_12288_config
	unsigned int M_DIV, N_DIV, SEL_PLL, SEL_DIV;	// DIV_12288_config

//	printk("p2001_cpufreq_driver_get\n");
	/*
	 * Save this threads cpus_allowed mask.
	 */
	cpus_allowed = current->cpus_allowed;

	/*
	 * Bind to the specified CPU.  When this call returns,
	 * we should be running on the right CPU.
	 */
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	BUG_ON(cpu != smp_processor_id());

	/* get current setting */
	M	= (P2001_TIMER->PLL_12288_config >>  0) & 0x00ff;
	P	= (P2001_TIMER->PLL_12288_config >>  8) & 0x003f;
	S	= (P2001_TIMER->PLL_12288_config >> 14) & 0x0003;
	N	= (P2001_TIMER->PLL_12288_config >> 16) & 0x03ff;
	PWRDN	= (P2001_TIMER->PLL_12288_config >> 26) & 0x0001;
	M_DIV	= (P2001_TIMER->DIV_12288_config >>  0) & 0x00ff;
	N_DIV	= (P2001_TIMER->DIV_12288_config >>  8) & 0x00ff;
	SEL_PLL	= (P2001_TIMER->DIV_12288_config >> 16) & 0x0001;
	SEL_DIV	= (P2001_TIMER->DIV_12288_config >> 17) & 0x0001;
//	printk("M=%d P=%d S=%d N=%d PWRDN=%d\n", M, P, S, N, PWRDN);
//	printk("M_DIV=%d N_DIV=%d SEL_PLL=%d SEL_DIV=%d\n", M_DIV, N_DIV, SEL_PLL, SEL_DIV);

	current_freq = 12288; // External 12.288 MHz oscillator
//	printk("cpufreq after OSC: %d\n", current_freq);

	switch (SEL_PLL) {
		case 0: // shortcut
			break;
		case 1: // PLL
			/* WARNING: 2^S=2 for S=0 (CodeSourcery ARM Q1A 2004) */
//			current_freq *= PWRDN ? 0 : ((M+8) / ( (2^S) * (P+2) )); // correct
			current_freq *= PWRDN ? 0 : ((M+8) / (P+2)); // working
			break;
		case 2: // 0
			current_freq = 0;
			break;
	}
//	printk("cpufreq after PLL: %d\n", current_freq);

	switch (SEL_DIV) {
		case 0: // shortcut
			break;
		case 1:
			current_freq /= (2*(N+1));
			break;
	}
//	printk("cpufreq after DIV: %d\n", current_freq);

	/*
	 * Restore the CPUs allowed mask.
	 */
	set_cpus_allowed(current, cpus_allowed);

	return current_freq;
}

static struct cpufreq_driver p2001_cpufreq_driver = {
	.name		= "P2001 cpufreq",
	.init		= p2001_cpufreq_driver_init,
	.verify		= p2001_cpufreq_driver_verify,
//	.setpolicy	= p2001_cpufreq_driver_setpolicy,
	.target		= p2001_cpufreq_driver_target,
	.get		= p2001_cpufreq_driver_get,
};

static int __init p2001_cpufreq_module_init(void)
{
//	printk("p2001_cpufreq_module_init\n");
	return cpufreq_register_driver(&p2001_cpufreq_driver);
}

static void __exit p2001_cpufreq_module_exit(void)
{
//	printk("p2001_cpufreq_module_exit\n");
	cpufreq_unregister_driver(&p2001_cpufreq_driver);
}

module_init(p2001_cpufreq_module_init);
module_exit(p2001_cpufreq_module_exit);

MODULE_AUTHOR("Tobias Lorenz");
MODULE_DESCRIPTION("P2001 cpu frequency scaling driver");
MODULE_LICENSE("GPL");

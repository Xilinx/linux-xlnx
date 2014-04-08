/*
 *  This file was based upon code in Powertweak Linux (http://powertweak.sf.net)
 *  (C) 2000-2003  Dave Jones, Arjan van de Ven, Janne Pänkälä,
 *                 Dominik Brodowski.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/io.h>

#include <asm/cpu_device_id.h>
#include <asm/msr.h>

#define POWERNOW_IOPORT 0xfff0          /* it doesn't matter where, as long
					   as it is unused */

#define PFX "powernow-k6: "
static unsigned int                     busfreq;   /* FSB, in 10 kHz */
static unsigned int                     max_multiplier;


/* Clock ratio multiplied by 10 - see table 27 in AMD#23446 */
static struct cpufreq_frequency_table clock_ratio[] = {
	{45,  /* 000 -> 4.5x */ 0},
	{50,  /* 001 -> 5.0x */ 0},
	{40,  /* 010 -> 4.0x */ 0},
	{55,  /* 011 -> 5.5x */ 0},
	{20,  /* 100 -> 2.0x */ 0},
	{30,  /* 101 -> 3.0x */ 0},
	{60,  /* 110 -> 6.0x */ 0},
	{35,  /* 111 -> 3.5x */ 0},
	{0, CPUFREQ_TABLE_END}
};


/**
 * powernow_k6_get_cpu_multiplier - returns the current FSB multiplier
 *
 *   Returns the current setting of the frequency multiplier. Core clock
 * speed is frequency of the Front-Side Bus multiplied with this value.
 */
static int powernow_k6_get_cpu_multiplier(void)
{
	u64 invalue = 0;
	u32 msrval;

	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue = inl(POWERNOW_IOPORT + 0x8);
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	return clock_ratio[(invalue >> 5)&7].driver_data;
}


/**
 * powernow_k6_target - set the PowerNow! multiplier
 * @best_i: clock_ratio[best_i] is the target multiplier
 *
 *   Tries to change the PowerNow! multiplier
 */
static int powernow_k6_target(struct cpufreq_policy *policy,
		unsigned int best_i)
{
	unsigned long outvalue = 0, invalue = 0;
	unsigned long msrval;
	struct cpufreq_freqs freqs;

	if (clock_ratio[best_i].driver_data > max_multiplier) {
		printk(KERN_ERR PFX "invalid target frequency\n");
		return -EINVAL;
	}

	freqs.old = busfreq * powernow_k6_get_cpu_multiplier();
	freqs.new = busfreq * clock_ratio[best_i].driver_data;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	/* we now need to transform best_i to the BVC format, see AMD#23446 */

	outvalue = (1<<12) | (1<<10) | (1<<9) | (best_i<<5);

	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue = inl(POWERNOW_IOPORT + 0x8);
	invalue = invalue & 0xf;
	outvalue = outvalue | invalue;
	outl(outvalue , (POWERNOW_IOPORT + 0x8));
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	return 0;
}


static int powernow_k6_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int i, f;

	if (policy->cpu != 0)
		return -ENODEV;

	/* get frequencies */
	max_multiplier = powernow_k6_get_cpu_multiplier();
	busfreq = cpu_khz / max_multiplier;

	/* table init */
	for (i = 0; (clock_ratio[i].frequency != CPUFREQ_TABLE_END); i++) {
		f = clock_ratio[i].driver_data;
		if (f > max_multiplier)
			clock_ratio[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			clock_ratio[i].frequency = busfreq * f;
	}

	/* cpuinfo and default policy values */
	policy->cpuinfo.transition_latency = 200000;

	return cpufreq_table_validate_and_show(policy, clock_ratio);
}


static int powernow_k6_cpu_exit(struct cpufreq_policy *policy)
{
	unsigned int i;
	for (i = 0; i < 8; i++) {
		if (i == max_multiplier)
			powernow_k6_target(policy, i);
	}
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static unsigned int powernow_k6_get(unsigned int cpu)
{
	unsigned int ret;
	ret = (busfreq * powernow_k6_get_cpu_multiplier());
	return ret;
}

static struct cpufreq_driver powernow_k6_driver = {
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernow_k6_target,
	.init		= powernow_k6_cpu_init,
	.exit		= powernow_k6_cpu_exit,
	.get		= powernow_k6_get,
	.name		= "powernow-k6",
	.attr		= cpufreq_generic_attr,
};

static const struct x86_cpu_id powernow_k6_ids[] = {
	{ X86_VENDOR_AMD, 5, 12 },
	{ X86_VENDOR_AMD, 5, 13 },
	{}
};
MODULE_DEVICE_TABLE(x86cpu, powernow_k6_ids);

/**
 * powernow_k6_init - initializes the k6 PowerNow! CPUFreq driver
 *
 *   Initializes the K6 PowerNow! support. Returns -ENODEV on unsupported
 * devices, -EINVAL or -ENOMEM on problems during initiatization, and zero
 * on success.
 */
static int __init powernow_k6_init(void)
{
	if (!x86_match_cpu(powernow_k6_ids))
		return -ENODEV;

	if (!request_region(POWERNOW_IOPORT, 16, "PowerNow!")) {
		printk(KERN_INFO PFX "PowerNow IOPORT region already used.\n");
		return -EIO;
	}

	if (cpufreq_register_driver(&powernow_k6_driver)) {
		release_region(POWERNOW_IOPORT, 16);
		return -EINVAL;
	}

	return 0;
}


/**
 * powernow_k6_exit - unregisters AMD K6-2+/3+ PowerNow! support
 *
 *   Unregisters AMD K6-2+ / K6-3+ PowerNow! support.
 */
static void __exit powernow_k6_exit(void)
{
	cpufreq_unregister_driver(&powernow_k6_driver);
	release_region(POWERNOW_IOPORT, 16);
}


MODULE_AUTHOR("Arjan van de Ven, Dave Jones <davej@redhat.com>, "
		"Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("PowerNow! driver for AMD K6-2+ / K6-3+ processors.");
MODULE_LICENSE("GPL");

module_init(powernow_k6_init);
module_exit(powernow_k6_exit);

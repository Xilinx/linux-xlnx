/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *  (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cputime.h>

static DEFINE_SPINLOCK(cpufreq_stats_lock);

struct cpufreq_stats {
	unsigned int total_trans;
	unsigned long long last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

static int cpufreq_stats_update(struct cpufreq_stats *stats)
{
	unsigned long long cur_time = get_jiffies_64();

	spin_lock(&cpufreq_stats_lock);
	stats->time_in_state[stats->last_index] += cur_time - stats->last_time;
	stats->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%d\n", policy->stats->total_trans);
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i;

	if (policy->fast_switch_enabled)
		return 0;

	cpufreq_stats_update(stats);
	for (i = 0; i < stats->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stats->freq_table[i],
			(unsigned long long)
			jiffies_64_to_clock_t(stats->time_in_state[i]));
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i, j;

	if (policy->fast_switch_enabled)
		return 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stats->freq_table[i]);
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stats->freq_table[i]);

		for (j = 0; j < stats->state_num; j++) {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stats->trans_table[i*stats->max_state+j]);
		}
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;
	return len;
}
cpufreq_freq_attr_ro(trans_table);
#endif

cpufreq_freq_attr_ro(total_trans);
cpufreq_freq_attr_ro(time_in_state);

static struct attribute *default_attrs[] = {
	&total_trans.attr,
	&time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static int freq_table_get_index(struct cpufreq_stats *stats, unsigned int freq)
{
	int index;
	for (index = 0; index < stats->max_state; index++)
		if (stats->freq_table[index] == freq)
			return index;
	return -1;
}

void cpufreq_stats_free_table(struct cpufreq_policy *policy)
{
	struct cpufreq_stats *stats = policy->stats;

	/* Already freed */
	if (!stats)
		return;

	pr_debug("%s: Free stats table\n", __func__);

	sysfs_remove_group(&policy->kobj, &stats_attr_group);
	kfree(stats->time_in_state);
	kfree(stats);
	policy->stats = NULL;
}

void cpufreq_stats_create_table(struct cpufreq_policy *policy)
{
	unsigned int i = 0, count = 0, ret = -ENOMEM;
	struct cpufreq_stats *stats;
	unsigned int alloc_size;
	struct cpufreq_frequency_table *pos, *table;

	/* We need cpufreq table for creating stats table */
	table = policy->freq_table;
	if (unlikely(!table))
		return;

	/* stats already initialized */
	if (policy->stats)
		return;

	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return;

	/* Find total allocation size */
	cpufreq_for_each_valid_entry(pos, table)
		count++;

	alloc_size = count * sizeof(int) + count * sizeof(u64);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	alloc_size += count * count * sizeof(int);
#endif

	/* Allocate memory for time_in_state/freq_table/trans_table in one go */
	stats->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stats->time_in_state)
		goto free_stat;

	stats->freq_table = (unsigned int *)(stats->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table = stats->freq_table + count;
#endif

	stats->max_state = count;

	/* Find valid-unique entries */
	cpufreq_for_each_valid_entry(pos, table)
		if (freq_table_get_index(stats, pos->frequency) == -1)
			stats->freq_table[i++] = pos->frequency;

	stats->state_num = i;
	stats->last_time = get_jiffies_64();
	stats->last_index = freq_table_get_index(stats, policy->cur);

	policy->stats = stats;
	ret = sysfs_create_group(&policy->kobj, &stats_attr_group);
	if (!ret)
		return;

	/* We failed, release resources */
	policy->stats = NULL;
	kfree(stats->time_in_state);
free_stat:
	kfree(stats);
}

void cpufreq_stats_record_transition(struct cpufreq_policy *policy,
				     unsigned int new_freq)
{
	struct cpufreq_stats *stats = policy->stats;
	int old_index, new_index;

	if (!stats) {
		pr_debug("%s: No stats found\n", __func__);
		return;
	}

	old_index = stats->last_index;
	new_index = freq_table_get_index(stats, new_freq);

	/* We can't do stats->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1 || old_index == new_index)
		return;

	cpufreq_stats_update(stats);

	stats->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table[old_index * stats->max_state + new_index]++;
#endif
	stats->total_trans++;
}

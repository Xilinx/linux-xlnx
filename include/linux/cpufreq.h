/*
 * linux/include/linux/cpufreq.h
 *
 * Copyright (C) 2001 Russell King
 *           (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

#include <linux/cpumask.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/notifier.h>
#include <linux/sysfs.h>

/*********************************************************************
 *                        CPUFREQ INTERFACE                          *
 *********************************************************************/
/*
 * Frequency values here are CPU kHz
 *
 * Maximum transition latency is in nanoseconds - if it's unknown,
 * CPUFREQ_ETERNAL shall be used.
 */

#define CPUFREQ_ETERNAL			(-1)
#define CPUFREQ_NAME_LEN		16
/* Print length for names. Extra 1 space for accomodating '\n' in prints */
#define CPUFREQ_NAME_PLEN		(CPUFREQ_NAME_LEN + 1)

struct cpufreq_governor;

struct cpufreq_freqs {
	unsigned int cpu;	/* cpu nr */
	unsigned int old;
	unsigned int new;
	u8 flags;		/* flags of cpufreq_driver, see below. */
};

struct cpufreq_cpuinfo {
	unsigned int		max_freq;
	unsigned int		min_freq;

	/* in 10^(-9) s = nanoseconds */
	unsigned int		transition_latency;
};

struct cpufreq_real_policy {
	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
	unsigned int		policy; /* see above */
	struct cpufreq_governor	*governor; /* see below */
};

struct cpufreq_policy {
	/* CPUs sharing clock, require sw coordination */
	cpumask_var_t		cpus;	/* Online CPUs only */
	cpumask_var_t		related_cpus; /* Online + Offline CPUs */

	unsigned int		shared_type; /* ACPI: ANY or ALL affected CPUs
						should set cpufreq */
	unsigned int		cpu;    /* cpu nr of CPU managing this policy */
	unsigned int		last_cpu; /* cpu nr of previous CPU that managed
					   * this policy */
	struct cpufreq_cpuinfo	cpuinfo;/* see above */

	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
	unsigned int		cur;    /* in kHz, only needed if cpufreq
					 * governors are used */
	unsigned int		policy; /* see above */
	struct cpufreq_governor	*governor; /* see below */
	void			*governor_data;
	bool			governor_enabled; /* governor start/stop flag */

	struct work_struct	update; /* if update_policy() needs to be
					 * called, but you're in IRQ context */

	struct cpufreq_real_policy	user_policy;

	struct list_head        policy_list;
	struct kobject		kobj;
	struct completion	kobj_unregister;

	/*
	 * The rules for this semaphore:
	 * - Any routine that wants to read from the policy structure will
	 *   do a down_read on this semaphore.
	 * - Any routine that will write to the policy structure and/or may take away
	 *   the policy altogether (eg. CPU hotplug), will hold this lock in write
	 *   mode before doing so.
	 *
	 * Additional rules:
	 * - Lock should not be held across
	 *     __cpufreq_governor(data, CPUFREQ_GOV_POLICY_EXIT);
	 */
	struct rw_semaphore	rwsem;
};

/* Only for ACPI */
#define CPUFREQ_SHARED_TYPE_NONE (0) /* None */
#define CPUFREQ_SHARED_TYPE_HW	 (1) /* HW does needed coordination */
#define CPUFREQ_SHARED_TYPE_ALL	 (2) /* All dependent CPUs should set freq */
#define CPUFREQ_SHARED_TYPE_ANY	 (3) /* Freq can be set from any dependent CPU*/

#ifdef CONFIG_CPU_FREQ
struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu);
void cpufreq_cpu_put(struct cpufreq_policy *policy);
#else
static inline struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu)
{
	return NULL;
}
static inline void cpufreq_cpu_put(struct cpufreq_policy *policy) { }
#endif

static inline bool policy_is_shared(struct cpufreq_policy *policy)
{
	return cpumask_weight(policy->cpus) > 1;
}

/* /sys/devices/system/cpu/cpufreq: entry point for global variables */
extern struct kobject *cpufreq_global_kobject;
int cpufreq_get_global_kobject(void);
void cpufreq_put_global_kobject(void);
int cpufreq_sysfs_create_file(const struct attribute *attr);
void cpufreq_sysfs_remove_file(const struct attribute *attr);

#ifdef CONFIG_CPU_FREQ
unsigned int cpufreq_get(unsigned int cpu);
unsigned int cpufreq_quick_get(unsigned int cpu);
unsigned int cpufreq_quick_get_max(unsigned int cpu);
void disable_cpufreq(void);

u64 get_cpu_idle_time(unsigned int cpu, u64 *wall, int io_busy);
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu);
int cpufreq_update_policy(unsigned int cpu);
bool have_governor_per_policy(void);
struct kobject *get_governor_parent_kobj(struct cpufreq_policy *policy);
#else
static inline unsigned int cpufreq_get(unsigned int cpu)
{
	return 0;
}
static inline unsigned int cpufreq_quick_get(unsigned int cpu)
{
	return 0;
}
static inline unsigned int cpufreq_quick_get_max(unsigned int cpu)
{
	return 0;
}
static inline void disable_cpufreq(void) { }
#endif

/*********************************************************************
 *                      CPUFREQ DRIVER INTERFACE                     *
 *********************************************************************/

#define CPUFREQ_RELATION_L 0  /* lowest frequency at or above target */
#define CPUFREQ_RELATION_H 1  /* highest frequency below or at target */

struct freq_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpufreq_policy *, char *);
	ssize_t (*store)(struct cpufreq_policy *, const char *, size_t count);
};

#define cpufreq_freq_attr_ro(_name)		\
static struct freq_attr _name =			\
__ATTR(_name, 0444, show_##_name, NULL)

#define cpufreq_freq_attr_ro_perm(_name, _perm)	\
static struct freq_attr _name =			\
__ATTR(_name, _perm, show_##_name, NULL)

#define cpufreq_freq_attr_rw(_name)		\
static struct freq_attr _name =			\
__ATTR(_name, 0644, show_##_name, store_##_name)

struct global_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *a, struct attribute *b,
			 const char *c, size_t count);
};

#define define_one_global_ro(_name)		\
static struct global_attr _name =		\
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_global_rw(_name)		\
static struct global_attr _name =		\
__ATTR(_name, 0644, show_##_name, store_##_name)


struct cpufreq_driver {
	char			name[CPUFREQ_NAME_LEN];
	u8			flags;

	/* needed by all drivers */
	int	(*init)		(struct cpufreq_policy *policy);
	int	(*verify)	(struct cpufreq_policy *policy);

	/* define one out of two */
	int	(*setpolicy)	(struct cpufreq_policy *policy);
	int	(*target)	(struct cpufreq_policy *policy,	/* Deprecated */
				 unsigned int target_freq,
				 unsigned int relation);
	int	(*target_index)	(struct cpufreq_policy *policy,
				 unsigned int index);

	/* should be defined, if possible */
	unsigned int	(*get)	(unsigned int cpu);

	/* optional */
	int	(*bios_limit)	(int cpu, unsigned int *limit);

	int	(*exit)		(struct cpufreq_policy *policy);
	int	(*suspend)	(struct cpufreq_policy *policy);
	int	(*resume)	(struct cpufreq_policy *policy);
	struct freq_attr	**attr;
};

/* flags */
#define CPUFREQ_STICKY		(1 << 0)	/* driver isn't removed even if
						   all ->init() calls failed */
#define CPUFREQ_CONST_LOOPS	(1 << 1)	/* loops_per_jiffy or other
						   kernel "constants" aren't
						   affected by frequency
						   transitions */
#define CPUFREQ_PM_NO_WARN	(1 << 2)	/* don't warn on suspend/resume
						   speed mismatches */

/*
 * This should be set by platforms having multiple clock-domains, i.e.
 * supporting multiple policies. With this sysfs directories of governor would
 * be created in cpu/cpu<num>/cpufreq/ directory and so they can use the same
 * governor with different tunables for different clusters.
 */
#define CPUFREQ_HAVE_GOVERNOR_PER_POLICY (1 << 3)

/*
 * Driver will do POSTCHANGE notifications from outside of their ->target()
 * routine and so must set cpufreq_driver->flags with this flag, so that core
 * can handle them specially.
 */
#define CPUFREQ_ASYNC_NOTIFICATION  (1 << 4)

int cpufreq_register_driver(struct cpufreq_driver *driver_data);
int cpufreq_unregister_driver(struct cpufreq_driver *driver_data);

const char *cpufreq_get_current_driver(void);

static inline void cpufreq_verify_within_limits(struct cpufreq_policy *policy,
		unsigned int min, unsigned int max)
{
	if (policy->min < min)
		policy->min = min;
	if (policy->max < min)
		policy->max = min;
	if (policy->min > max)
		policy->min = max;
	if (policy->max > max)
		policy->max = max;
	if (policy->min > policy->max)
		policy->min = policy->max;
	return;
}

static inline void
cpufreq_verify_within_cpu_limits(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
			policy->cpuinfo.max_freq);
}

/*********************************************************************
 *                     CPUFREQ NOTIFIER INTERFACE                    *
 *********************************************************************/

#define CPUFREQ_TRANSITION_NOTIFIER	(0)
#define CPUFREQ_POLICY_NOTIFIER		(1)

/* Transition notifiers */
#define CPUFREQ_PRECHANGE		(0)
#define CPUFREQ_POSTCHANGE		(1)
#define CPUFREQ_RESUMECHANGE		(8)
#define CPUFREQ_SUSPENDCHANGE		(9)

/* Policy Notifiers  */
#define CPUFREQ_ADJUST			(0)
#define CPUFREQ_INCOMPATIBLE		(1)
#define CPUFREQ_NOTIFY			(2)
#define CPUFREQ_START			(3)
#define CPUFREQ_UPDATE_POLICY_CPU	(4)

#ifdef CONFIG_CPU_FREQ
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list);
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list);

void cpufreq_notify_transition(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, unsigned int state);

#else /* CONFIG_CPU_FREQ */
static inline int cpufreq_register_notifier(struct notifier_block *nb,
						unsigned int list)
{
	return 0;
}
static inline int cpufreq_unregister_notifier(struct notifier_block *nb,
						unsigned int list)
{
	return 0;
}
#endif /* !CONFIG_CPU_FREQ */

/**
 * cpufreq_scale - "old * mult / div" calculation for large values (32-bit-arch
 * safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 *
 * new = old * mult / div
 */
static inline unsigned long cpufreq_scale(unsigned long old, u_int div,
		u_int mult)
{
#if BITS_PER_LONG == 32
	u64 result = ((u64) old) * ((u64) mult);
	do_div(result, div);
	return (unsigned long) result;

#elif BITS_PER_LONG == 64
	unsigned long result = old * ((u64) mult);
	result /= div;
	return result;
#endif
}

/*********************************************************************
 *                          CPUFREQ GOVERNORS                        *
 *********************************************************************/

/*
 * If (cpufreq_driver->target) exists, the ->governor decides what frequency
 * within the limits is used. If (cpufreq_driver->setpolicy> exists, these
 * two generic policies are available:
 */
#define CPUFREQ_POLICY_POWERSAVE	(1)
#define CPUFREQ_POLICY_PERFORMANCE	(2)

/* Governor Events */
#define CPUFREQ_GOV_START	1
#define CPUFREQ_GOV_STOP	2
#define CPUFREQ_GOV_LIMITS	3
#define CPUFREQ_GOV_POLICY_INIT	4
#define CPUFREQ_GOV_POLICY_EXIT	5

struct cpufreq_governor {
	char	name[CPUFREQ_NAME_LEN];
	int	initialized;
	int	(*governor)	(struct cpufreq_policy *policy,
				 unsigned int event);
	ssize_t	(*show_setspeed)	(struct cpufreq_policy *policy,
					 char *buf);
	int	(*store_setspeed)	(struct cpufreq_policy *policy,
					 unsigned int freq);
	unsigned int max_transition_latency; /* HW must be able to switch to
			next freq faster than this value in nano secs or we
			will fallback to performance governor */
	struct list_head	governor_list;
	struct module		*owner;
};

/* Pass a target to the cpufreq driver */
int cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);
int __cpufreq_driver_target(struct cpufreq_policy *policy,
				   unsigned int target_freq,
				   unsigned int relation);
int cpufreq_register_governor(struct cpufreq_governor *governor);
void cpufreq_unregister_governor(struct cpufreq_governor *governor);

/* CPUFREQ DEFAULT GOVERNOR */
/*
 * Performance governor is fallback governor if any other gov failed to auto
 * load due latency restrictions
 */
#ifdef CONFIG_CPU_FREQ_GOV_PERFORMANCE
extern struct cpufreq_governor cpufreq_gov_performance;
#endif
#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE
#define CPUFREQ_DEFAULT_GOVERNOR	(&cpufreq_gov_performance)
#elif defined(CONFIG_CPU_FREQ_DEFAULT_GOV_POWERSAVE)
extern struct cpufreq_governor cpufreq_gov_powersave;
#define CPUFREQ_DEFAULT_GOVERNOR	(&cpufreq_gov_powersave)
#elif defined(CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE)
extern struct cpufreq_governor cpufreq_gov_userspace;
#define CPUFREQ_DEFAULT_GOVERNOR	(&cpufreq_gov_userspace)
#elif defined(CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND)
extern struct cpufreq_governor cpufreq_gov_ondemand;
#define CPUFREQ_DEFAULT_GOVERNOR	(&cpufreq_gov_ondemand)
#elif defined(CONFIG_CPU_FREQ_DEFAULT_GOV_CONSERVATIVE)
extern struct cpufreq_governor cpufreq_gov_conservative;
#define CPUFREQ_DEFAULT_GOVERNOR	(&cpufreq_gov_conservative)
#endif

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

#define CPUFREQ_ENTRY_INVALID ~0
#define CPUFREQ_TABLE_END     ~1

struct cpufreq_frequency_table {
	unsigned int	driver_data; /* driver specific data, not used by core */
	unsigned int	frequency; /* kHz - doesn't need to be in ascending
				    * order */
};

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table);

int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table);
int cpufreq_generic_frequency_table_verify(struct cpufreq_policy *policy);

int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index);

void cpufreq_frequency_table_update_policy_cpu(struct cpufreq_policy *policy);
ssize_t cpufreq_show_cpus(const struct cpumask *mask, char *buf);

/* the following funtion is for cpufreq core use only */
struct cpufreq_frequency_table *cpufreq_frequency_get_table(unsigned int cpu);

/* the following are really really optional */
extern struct freq_attr cpufreq_freq_attr_scaling_available_freqs;
extern struct freq_attr *cpufreq_generic_attr[];
void cpufreq_frequency_table_get_attr(struct cpufreq_frequency_table *table,
				      unsigned int cpu);
void cpufreq_frequency_table_put_attr(unsigned int cpu);
int cpufreq_table_validate_and_show(struct cpufreq_policy *policy,
				      struct cpufreq_frequency_table *table);

int cpufreq_generic_init(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table,
		unsigned int transition_latency);
static inline int cpufreq_generic_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

#endif /* _LINUX_CPUFREQ_H */

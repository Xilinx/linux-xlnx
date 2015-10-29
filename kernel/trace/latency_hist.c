/*
 * kernel/trace/latency_hist.c
 *
 * Add support for histograms of preemption-off latency and
 * interrupt-off latency and wakeup latency, it depends on
 * Real-Time Preemption Support.
 *
 *  Copyright (C) 2005 MontaVista Software, Inc.
 *  Yi Yang <yyang@ch.mvista.com>
 *
 *  Converted to work with the new latency tracer.
 *  Copyright (C) 2008 Red Hat, Inc.
 *    Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <asm/div64.h>

#include "trace.h"
#include <trace/events/sched.h>

#define NSECS_PER_USECS 1000L

#define CREATE_TRACE_POINTS
#include <trace/events/hist.h>

enum {
	IRQSOFF_LATENCY = 0,
	PREEMPTOFF_LATENCY,
	PREEMPTIRQSOFF_LATENCY,
	WAKEUP_LATENCY,
	WAKEUP_LATENCY_SHAREDPRIO,
	MISSED_TIMER_OFFSETS,
	TIMERANDWAKEUP_LATENCY,
	MAX_LATENCY_TYPE,
};

#define MAX_ENTRY_NUM 10240

struct hist_data {
	atomic_t hist_mode; /* 0 log, 1 don't log */
	long offset; /* set it to MAX_ENTRY_NUM/2 for a bipolar scale */
	long min_lat;
	long max_lat;
	unsigned long long below_hist_bound_samples;
	unsigned long long above_hist_bound_samples;
	long long accumulate_lat;
	unsigned long long total_samples;
	unsigned long long hist_array[MAX_ENTRY_NUM];
};

struct enable_data {
	int latency_type;
	int enabled;
};

static char *latency_hist_dir_root = "latency_hist";

#ifdef CONFIG_INTERRUPT_OFF_HIST
static DEFINE_PER_CPU(struct hist_data, irqsoff_hist);
static char *irqsoff_hist_dir = "irqsoff";
static DEFINE_PER_CPU(cycles_t, hist_irqsoff_start);
static DEFINE_PER_CPU(int, hist_irqsoff_counting);
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
static DEFINE_PER_CPU(struct hist_data, preemptoff_hist);
static char *preemptoff_hist_dir = "preemptoff";
static DEFINE_PER_CPU(cycles_t, hist_preemptoff_start);
static DEFINE_PER_CPU(int, hist_preemptoff_counting);
#endif

#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
static DEFINE_PER_CPU(struct hist_data, preemptirqsoff_hist);
static char *preemptirqsoff_hist_dir = "preemptirqsoff";
static DEFINE_PER_CPU(cycles_t, hist_preemptirqsoff_start);
static DEFINE_PER_CPU(int, hist_preemptirqsoff_counting);
#endif

#if defined(CONFIG_PREEMPT_OFF_HIST) || defined(CONFIG_INTERRUPT_OFF_HIST)
static notrace void probe_preemptirqsoff_hist(void *v, int reason, int start);
static struct enable_data preemptirqsoff_enabled_data = {
	.latency_type = PREEMPTIRQSOFF_LATENCY,
	.enabled = 0,
};
#endif

#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
struct maxlatproc_data {
	char comm[FIELD_SIZEOF(struct task_struct, comm)];
	char current_comm[FIELD_SIZEOF(struct task_struct, comm)];
	int pid;
	int current_pid;
	int prio;
	int current_prio;
	long latency;
	long timeroffset;
	cycle_t timestamp;
};
#endif

#ifdef CONFIG_WAKEUP_LATENCY_HIST
static DEFINE_PER_CPU(struct hist_data, wakeup_latency_hist);
static DEFINE_PER_CPU(struct hist_data, wakeup_latency_hist_sharedprio);
static char *wakeup_latency_hist_dir = "wakeup";
static char *wakeup_latency_hist_dir_sharedprio = "sharedprio";
static notrace void probe_wakeup_latency_hist_start(void *v,
	struct task_struct *p, int success);
static notrace void probe_wakeup_latency_hist_stop(void *v,
	struct task_struct *prev, struct task_struct *next);
static notrace void probe_sched_migrate_task(void *,
	struct task_struct *task, int cpu);
static struct enable_data wakeup_latency_enabled_data = {
	.latency_type = WAKEUP_LATENCY,
	.enabled = 0,
};
static DEFINE_PER_CPU(struct maxlatproc_data, wakeup_maxlatproc);
static DEFINE_PER_CPU(struct maxlatproc_data, wakeup_maxlatproc_sharedprio);
static DEFINE_PER_CPU(struct task_struct *, wakeup_task);
static DEFINE_PER_CPU(int, wakeup_sharedprio);
static unsigned long wakeup_pid;
#endif

#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
static DEFINE_PER_CPU(struct hist_data, missed_timer_offsets);
static char *missed_timer_offsets_dir = "missed_timer_offsets";
static notrace void probe_hrtimer_interrupt(void *v, int cpu,
	long long offset, struct task_struct *curr, struct task_struct *task);
static struct enable_data missed_timer_offsets_enabled_data = {
	.latency_type = MISSED_TIMER_OFFSETS,
	.enabled = 0,
};
static DEFINE_PER_CPU(struct maxlatproc_data, missed_timer_offsets_maxlatproc);
static unsigned long missed_timer_offsets_pid;
#endif

#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
static DEFINE_PER_CPU(struct hist_data, timerandwakeup_latency_hist);
static char *timerandwakeup_latency_hist_dir = "timerandwakeup";
static struct enable_data timerandwakeup_enabled_data = {
	.latency_type = TIMERANDWAKEUP_LATENCY,
	.enabled = 0,
};
static DEFINE_PER_CPU(struct maxlatproc_data, timerandwakeup_maxlatproc);
#endif

void notrace latency_hist(int latency_type, int cpu, long latency,
			  long timeroffset, cycle_t stop,
			  struct task_struct *p)
{
	struct hist_data *my_hist;
#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
	struct maxlatproc_data *mp = NULL;
#endif

	if (!cpu_possible(cpu) || latency_type < 0 ||
	    latency_type >= MAX_LATENCY_TYPE)
		return;

	switch (latency_type) {
#ifdef CONFIG_INTERRUPT_OFF_HIST
	case IRQSOFF_LATENCY:
		my_hist = &per_cpu(irqsoff_hist, cpu);
		break;
#endif
#ifdef CONFIG_PREEMPT_OFF_HIST
	case PREEMPTOFF_LATENCY:
		my_hist = &per_cpu(preemptoff_hist, cpu);
		break;
#endif
#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
	case PREEMPTIRQSOFF_LATENCY:
		my_hist = &per_cpu(preemptirqsoff_hist, cpu);
		break;
#endif
#ifdef CONFIG_WAKEUP_LATENCY_HIST
	case WAKEUP_LATENCY:
		my_hist = &per_cpu(wakeup_latency_hist, cpu);
		mp = &per_cpu(wakeup_maxlatproc, cpu);
		break;
	case WAKEUP_LATENCY_SHAREDPRIO:
		my_hist = &per_cpu(wakeup_latency_hist_sharedprio, cpu);
		mp = &per_cpu(wakeup_maxlatproc_sharedprio, cpu);
		break;
#endif
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
	case MISSED_TIMER_OFFSETS:
		my_hist = &per_cpu(missed_timer_offsets, cpu);
		mp = &per_cpu(missed_timer_offsets_maxlatproc, cpu);
		break;
#endif
#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
	case TIMERANDWAKEUP_LATENCY:
		my_hist = &per_cpu(timerandwakeup_latency_hist, cpu);
		mp = &per_cpu(timerandwakeup_maxlatproc, cpu);
		break;
#endif

	default:
		return;
	}

	latency += my_hist->offset;

	if (atomic_read(&my_hist->hist_mode) == 0)
		return;

	if (latency < 0 || latency >= MAX_ENTRY_NUM) {
		if (latency < 0)
			my_hist->below_hist_bound_samples++;
		else
			my_hist->above_hist_bound_samples++;
	} else
		my_hist->hist_array[latency]++;

	if (unlikely(latency > my_hist->max_lat ||
	    my_hist->min_lat == LONG_MAX)) {
#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
		if (latency_type == WAKEUP_LATENCY ||
		    latency_type == WAKEUP_LATENCY_SHAREDPRIO ||
		    latency_type == MISSED_TIMER_OFFSETS ||
		    latency_type == TIMERANDWAKEUP_LATENCY) {
			strncpy(mp->comm, p->comm, sizeof(mp->comm));
			strncpy(mp->current_comm, current->comm,
			    sizeof(mp->current_comm));
			mp->pid = task_pid_nr(p);
			mp->current_pid = task_pid_nr(current);
			mp->prio = p->prio;
			mp->current_prio = current->prio;
			mp->latency = latency;
			mp->timeroffset = timeroffset;
			mp->timestamp = stop;
		}
#endif
		my_hist->max_lat = latency;
	}
	if (unlikely(latency < my_hist->min_lat))
		my_hist->min_lat = latency;
	my_hist->total_samples++;
	my_hist->accumulate_lat += latency;
}

static void *l_start(struct seq_file *m, loff_t *pos)
{
	loff_t *index_ptr = NULL;
	loff_t index = *pos;
	struct hist_data *my_hist = m->private;

	if (index == 0) {
		char minstr[32], avgstr[32], maxstr[32];

		atomic_dec(&my_hist->hist_mode);

		if (likely(my_hist->total_samples)) {
			long avg = (long) div64_s64(my_hist->accumulate_lat,
			    my_hist->total_samples);
			snprintf(minstr, sizeof(minstr), "%ld",
			    my_hist->min_lat - my_hist->offset);
			snprintf(avgstr, sizeof(avgstr), "%ld",
			    avg - my_hist->offset);
			snprintf(maxstr, sizeof(maxstr), "%ld",
			    my_hist->max_lat - my_hist->offset);
		} else {
			strcpy(minstr, "<undef>");
			strcpy(avgstr, minstr);
			strcpy(maxstr, minstr);
		}

		seq_printf(m, "#Minimum latency: %s microseconds\n"
			   "#Average latency: %s microseconds\n"
			   "#Maximum latency: %s microseconds\n"
			   "#Total samples: %llu\n"
			   "#There are %llu samples lower than %ld"
			   " microseconds.\n"
			   "#There are %llu samples greater or equal"
			   " than %ld microseconds.\n"
			   "#usecs\t%16s\n",
			   minstr, avgstr, maxstr,
			   my_hist->total_samples,
			   my_hist->below_hist_bound_samples,
			   -my_hist->offset,
			   my_hist->above_hist_bound_samples,
			   MAX_ENTRY_NUM - my_hist->offset,
			   "samples");
	}
	if (index < MAX_ENTRY_NUM) {
		index_ptr = kmalloc(sizeof(loff_t), GFP_KERNEL);
		if (index_ptr)
			*index_ptr = index;
	}

	return index_ptr;
}

static void *l_next(struct seq_file *m, void *p, loff_t *pos)
{
	loff_t *index_ptr = p;
	struct hist_data *my_hist = m->private;

	if (++*pos >= MAX_ENTRY_NUM) {
		atomic_inc(&my_hist->hist_mode);
		return NULL;
	}
	*index_ptr = *pos;
	return index_ptr;
}

static void l_stop(struct seq_file *m, void *p)
{
	kfree(p);
}

static int l_show(struct seq_file *m, void *p)
{
	int index = *(loff_t *) p;
	struct hist_data *my_hist = m->private;

	seq_printf(m, "%6ld\t%16llu\n", index - my_hist->offset,
	    my_hist->hist_array[index]);
	return 0;
}

static const struct seq_operations latency_hist_seq_op = {
	.start = l_start,
	.next  = l_next,
	.stop  = l_stop,
	.show  = l_show
};

static int latency_hist_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &latency_hist_seq_op);
	if (!ret) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations latency_hist_fops = {
	.open = latency_hist_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
static void clear_maxlatprocdata(struct maxlatproc_data *mp)
{
	mp->comm[0] = mp->current_comm[0] = '\0';
	mp->prio = mp->current_prio = mp->pid = mp->current_pid =
	    mp->latency = mp->timeroffset = -1;
	mp->timestamp = 0;
}
#endif

static void hist_reset(struct hist_data *hist)
{
	atomic_dec(&hist->hist_mode);

	memset(hist->hist_array, 0, sizeof(hist->hist_array));
	hist->below_hist_bound_samples = 0ULL;
	hist->above_hist_bound_samples = 0ULL;
	hist->min_lat = LONG_MAX;
	hist->max_lat = LONG_MIN;
	hist->total_samples = 0ULL;
	hist->accumulate_lat = 0LL;

	atomic_inc(&hist->hist_mode);
}

static ssize_t
latency_hist_reset(struct file *file, const char __user *a,
		   size_t size, loff_t *off)
{
	int cpu;
	struct hist_data *hist = NULL;
#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
	struct maxlatproc_data *mp = NULL;
#endif
	off_t latency_type = (off_t) file->private_data;

	for_each_online_cpu(cpu) {

		switch (latency_type) {
#ifdef CONFIG_PREEMPT_OFF_HIST
		case PREEMPTOFF_LATENCY:
			hist = &per_cpu(preemptoff_hist, cpu);
			break;
#endif
#ifdef CONFIG_INTERRUPT_OFF_HIST
		case IRQSOFF_LATENCY:
			hist = &per_cpu(irqsoff_hist, cpu);
			break;
#endif
#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
		case PREEMPTIRQSOFF_LATENCY:
			hist = &per_cpu(preemptirqsoff_hist, cpu);
			break;
#endif
#ifdef CONFIG_WAKEUP_LATENCY_HIST
		case WAKEUP_LATENCY:
			hist = &per_cpu(wakeup_latency_hist, cpu);
			mp = &per_cpu(wakeup_maxlatproc, cpu);
			break;
		case WAKEUP_LATENCY_SHAREDPRIO:
			hist = &per_cpu(wakeup_latency_hist_sharedprio, cpu);
			mp = &per_cpu(wakeup_maxlatproc_sharedprio, cpu);
			break;
#endif
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
		case MISSED_TIMER_OFFSETS:
			hist = &per_cpu(missed_timer_offsets, cpu);
			mp = &per_cpu(missed_timer_offsets_maxlatproc, cpu);
			break;
#endif
#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
		case TIMERANDWAKEUP_LATENCY:
			hist = &per_cpu(timerandwakeup_latency_hist, cpu);
			mp = &per_cpu(timerandwakeup_maxlatproc, cpu);
			break;
#endif
		}

		hist_reset(hist);
#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
		if (latency_type == WAKEUP_LATENCY ||
		    latency_type == WAKEUP_LATENCY_SHAREDPRIO ||
		    latency_type == MISSED_TIMER_OFFSETS ||
		    latency_type == TIMERANDWAKEUP_LATENCY)
			clear_maxlatprocdata(mp);
#endif
	}

	return size;
}

#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
static ssize_t
show_pid(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;
	unsigned long *this_pid = file->private_data;

	r = snprintf(buf, sizeof(buf), "%lu\n", *this_pid);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t do_pid(struct file *file, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	char buf[64];
	unsigned long pid;
	unsigned long *this_pid = file->private_data;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = '\0';

	if (kstrtoul(buf, 10, &pid))
		return -EINVAL;

	*this_pid = pid;

	return cnt;
}
#endif

#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
static ssize_t
show_maxlatproc(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int r;
	struct maxlatproc_data *mp = file->private_data;
	int strmaxlen = (TASK_COMM_LEN * 2) + (8 * 8);
	unsigned long long t;
	unsigned long usecs, secs;
	char *buf;

	if (mp->pid == -1 || mp->current_pid == -1) {
		buf = "(none)\n";
		return simple_read_from_buffer(ubuf, cnt, ppos, buf,
		    strlen(buf));
	}

	buf = kmalloc(strmaxlen, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	t = ns2usecs(mp->timestamp);
	usecs = do_div(t, USEC_PER_SEC);
	secs = (unsigned long) t;
	r = snprintf(buf, strmaxlen,
	    "%d %d %ld (%ld) %s <- %d %d %s %lu.%06lu\n", mp->pid,
	    MAX_RT_PRIO-1 - mp->prio, mp->latency, mp->timeroffset, mp->comm,
	    mp->current_pid, MAX_RT_PRIO-1 - mp->current_prio, mp->current_comm,
	    secs, usecs);
	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	return r;
}
#endif

static ssize_t
show_enable(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	struct enable_data *ed = file->private_data;
	int r;

	r = snprintf(buf, sizeof(buf), "%d\n", ed->enabled);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
do_enable(struct file *file, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	long enable;
	struct enable_data *ed = file->private_data;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	if (kstrtoul(buf, 10, &enable))
		return -EINVAL;

	if ((enable && ed->enabled) || (!enable && !ed->enabled))
		return cnt;

	if (enable) {
		int ret;

		switch (ed->latency_type) {
#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
		case PREEMPTIRQSOFF_LATENCY:
			ret = register_trace_preemptirqsoff_hist(
			    probe_preemptirqsoff_hist, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_preemptirqsoff_hist "
				    "to trace_preemptirqsoff_hist\n");
				return ret;
			}
			break;
#endif
#ifdef CONFIG_WAKEUP_LATENCY_HIST
		case WAKEUP_LATENCY:
			ret = register_trace_sched_wakeup(
			    probe_wakeup_latency_hist_start, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_wakeup_latency_hist_start "
				    "to trace_sched_wakeup\n");
				return ret;
			}
			ret = register_trace_sched_wakeup_new(
			    probe_wakeup_latency_hist_start, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_wakeup_latency_hist_start "
				    "to trace_sched_wakeup_new\n");
				unregister_trace_sched_wakeup(
				    probe_wakeup_latency_hist_start, NULL);
				return ret;
			}
			ret = register_trace_sched_switch(
			    probe_wakeup_latency_hist_stop, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_wakeup_latency_hist_stop "
				    "to trace_sched_switch\n");
				unregister_trace_sched_wakeup(
				    probe_wakeup_latency_hist_start, NULL);
				unregister_trace_sched_wakeup_new(
				    probe_wakeup_latency_hist_start, NULL);
				return ret;
			}
			ret = register_trace_sched_migrate_task(
			    probe_sched_migrate_task, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_sched_migrate_task "
				    "to trace_sched_migrate_task\n");
				unregister_trace_sched_wakeup(
				    probe_wakeup_latency_hist_start, NULL);
				unregister_trace_sched_wakeup_new(
				    probe_wakeup_latency_hist_start, NULL);
				unregister_trace_sched_switch(
				    probe_wakeup_latency_hist_stop, NULL);
				return ret;
			}
			break;
#endif
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
		case MISSED_TIMER_OFFSETS:
			ret = register_trace_hrtimer_interrupt(
			    probe_hrtimer_interrupt, NULL);
			if (ret) {
				pr_info("wakeup trace: Couldn't assign "
				    "probe_hrtimer_interrupt "
				    "to trace_hrtimer_interrupt\n");
				return ret;
			}
			break;
#endif
#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
		case TIMERANDWAKEUP_LATENCY:
			if (!wakeup_latency_enabled_data.enabled ||
			    !missed_timer_offsets_enabled_data.enabled)
				return -EINVAL;
			break;
#endif
		default:
			break;
		}
	} else {
		switch (ed->latency_type) {
#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
		case PREEMPTIRQSOFF_LATENCY:
			{
				int cpu;

				unregister_trace_preemptirqsoff_hist(
				    probe_preemptirqsoff_hist, NULL);
				for_each_online_cpu(cpu) {
#ifdef CONFIG_INTERRUPT_OFF_HIST
					per_cpu(hist_irqsoff_counting,
					    cpu) = 0;
#endif
#ifdef CONFIG_PREEMPT_OFF_HIST
					per_cpu(hist_preemptoff_counting,
					    cpu) = 0;
#endif
#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
					per_cpu(hist_preemptirqsoff_counting,
					    cpu) = 0;
#endif
				}
			}
			break;
#endif
#ifdef CONFIG_WAKEUP_LATENCY_HIST
		case WAKEUP_LATENCY:
			{
				int cpu;

				unregister_trace_sched_wakeup(
				    probe_wakeup_latency_hist_start, NULL);
				unregister_trace_sched_wakeup_new(
				    probe_wakeup_latency_hist_start, NULL);
				unregister_trace_sched_switch(
				    probe_wakeup_latency_hist_stop, NULL);
				unregister_trace_sched_migrate_task(
				    probe_sched_migrate_task, NULL);

				for_each_online_cpu(cpu) {
					per_cpu(wakeup_task, cpu) = NULL;
					per_cpu(wakeup_sharedprio, cpu) = 0;
				}
			}
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
			timerandwakeup_enabled_data.enabled = 0;
#endif
			break;
#endif
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
		case MISSED_TIMER_OFFSETS:
			unregister_trace_hrtimer_interrupt(
			    probe_hrtimer_interrupt, NULL);
#ifdef CONFIG_WAKEUP_LATENCY_HIST
			timerandwakeup_enabled_data.enabled = 0;
#endif
			break;
#endif
		default:
			break;
		}
	}
	ed->enabled = enable;
	return cnt;
}

static const struct file_operations latency_hist_reset_fops = {
	.open = tracing_open_generic,
	.write = latency_hist_reset,
};

static const struct file_operations enable_fops = {
	.open = tracing_open_generic,
	.read = show_enable,
	.write = do_enable,
};

#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
static const struct file_operations pid_fops = {
	.open = tracing_open_generic,
	.read = show_pid,
	.write = do_pid,
};

static const struct file_operations maxlatproc_fops = {
	.open = tracing_open_generic,
	.read = show_maxlatproc,
};
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
static notrace void probe_preemptirqsoff_hist(void *v, int reason,
	int starthist)
{
	int cpu = raw_smp_processor_id();
	int time_set = 0;

	if (starthist) {
		cycle_t uninitialized_var(start);

		if (!preempt_count() && !irqs_disabled())
			return;

#ifdef CONFIG_INTERRUPT_OFF_HIST
		if ((reason == IRQS_OFF || reason == TRACE_START) &&
		    !per_cpu(hist_irqsoff_counting, cpu)) {
			per_cpu(hist_irqsoff_counting, cpu) = 1;
			start = ftrace_now(cpu);
			time_set++;
			per_cpu(hist_irqsoff_start, cpu) = start;
		}
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
		if ((reason == PREEMPT_OFF || reason == TRACE_START) &&
		    !per_cpu(hist_preemptoff_counting, cpu)) {
			per_cpu(hist_preemptoff_counting, cpu) = 1;
			if (!(time_set++))
				start = ftrace_now(cpu);
			per_cpu(hist_preemptoff_start, cpu) = start;
		}
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
		if (per_cpu(hist_irqsoff_counting, cpu) &&
		    per_cpu(hist_preemptoff_counting, cpu) &&
		    !per_cpu(hist_preemptirqsoff_counting, cpu)) {
			per_cpu(hist_preemptirqsoff_counting, cpu) = 1;
			if (!time_set)
				start = ftrace_now(cpu);
			per_cpu(hist_preemptirqsoff_start, cpu) = start;
		}
#endif
	} else {
		cycle_t uninitialized_var(stop);

#ifdef CONFIG_INTERRUPT_OFF_HIST
		if ((reason == IRQS_ON || reason == TRACE_STOP) &&
		    per_cpu(hist_irqsoff_counting, cpu)) {
			cycle_t start = per_cpu(hist_irqsoff_start, cpu);

			stop = ftrace_now(cpu);
			time_set++;
			if (start) {
				long latency = ((long) (stop - start)) /
				    NSECS_PER_USECS;

				latency_hist(IRQSOFF_LATENCY, cpu, latency, 0,
				    stop, NULL);
			}
			per_cpu(hist_irqsoff_counting, cpu) = 0;
		}
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
		if ((reason == PREEMPT_ON || reason == TRACE_STOP) &&
		    per_cpu(hist_preemptoff_counting, cpu)) {
			cycle_t start = per_cpu(hist_preemptoff_start, cpu);

			if (!(time_set++))
				stop = ftrace_now(cpu);
			if (start) {
				long latency = ((long) (stop - start)) /
				    NSECS_PER_USECS;

				latency_hist(PREEMPTOFF_LATENCY, cpu, latency,
				    0, stop, NULL);
			}
			per_cpu(hist_preemptoff_counting, cpu) = 0;
		}
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
		if ((!per_cpu(hist_irqsoff_counting, cpu) ||
		     !per_cpu(hist_preemptoff_counting, cpu)) &&
		   per_cpu(hist_preemptirqsoff_counting, cpu)) {
			cycle_t start = per_cpu(hist_preemptirqsoff_start, cpu);

			if (!time_set)
				stop = ftrace_now(cpu);
			if (start) {
				long latency = ((long) (stop - start)) /
				    NSECS_PER_USECS;

				latency_hist(PREEMPTIRQSOFF_LATENCY, cpu,
				    latency, 0, stop, NULL);
			}
			per_cpu(hist_preemptirqsoff_counting, cpu) = 0;
		}
#endif
	}
}
#endif

#ifdef CONFIG_WAKEUP_LATENCY_HIST
static DEFINE_RAW_SPINLOCK(wakeup_lock);
static notrace void probe_sched_migrate_task(void *v, struct task_struct *task,
	int cpu)
{
	int old_cpu = task_cpu(task);

	if (cpu != old_cpu) {
		unsigned long flags;
		struct task_struct *cpu_wakeup_task;

		raw_spin_lock_irqsave(&wakeup_lock, flags);

		cpu_wakeup_task = per_cpu(wakeup_task, old_cpu);
		if (task == cpu_wakeup_task) {
			put_task_struct(cpu_wakeup_task);
			per_cpu(wakeup_task, old_cpu) = NULL;
			cpu_wakeup_task = per_cpu(wakeup_task, cpu) = task;
			get_task_struct(cpu_wakeup_task);
		}

		raw_spin_unlock_irqrestore(&wakeup_lock, flags);
	}
}

static notrace void probe_wakeup_latency_hist_start(void *v,
	struct task_struct *p, int success)
{
	unsigned long flags;
	struct task_struct *curr = current;
	int cpu = task_cpu(p);
	struct task_struct *cpu_wakeup_task;

	raw_spin_lock_irqsave(&wakeup_lock, flags);

	cpu_wakeup_task = per_cpu(wakeup_task, cpu);

	if (wakeup_pid) {
		if ((cpu_wakeup_task && p->prio == cpu_wakeup_task->prio) ||
		    p->prio == curr->prio)
			per_cpu(wakeup_sharedprio, cpu) = 1;
		if (likely(wakeup_pid != task_pid_nr(p)))
			goto out;
	} else {
		if (likely(!rt_task(p)) ||
		    (cpu_wakeup_task && p->prio > cpu_wakeup_task->prio) ||
		    p->prio > curr->prio)
			goto out;
		if ((cpu_wakeup_task && p->prio == cpu_wakeup_task->prio) ||
		    p->prio == curr->prio)
			per_cpu(wakeup_sharedprio, cpu) = 1;
	}

	if (cpu_wakeup_task)
		put_task_struct(cpu_wakeup_task);
	cpu_wakeup_task = per_cpu(wakeup_task, cpu) = p;
	get_task_struct(cpu_wakeup_task);
	cpu_wakeup_task->preempt_timestamp_hist =
		ftrace_now(raw_smp_processor_id());
out:
	raw_spin_unlock_irqrestore(&wakeup_lock, flags);
}

static notrace void probe_wakeup_latency_hist_stop(void *v,
	struct task_struct *prev, struct task_struct *next)
{
	unsigned long flags;
	int cpu = task_cpu(next);
	long latency;
	cycle_t stop;
	struct task_struct *cpu_wakeup_task;

	raw_spin_lock_irqsave(&wakeup_lock, flags);

	cpu_wakeup_task = per_cpu(wakeup_task, cpu);

	if (cpu_wakeup_task == NULL)
		goto out;

	/* Already running? */
	if (unlikely(current == cpu_wakeup_task))
		goto out_reset;

	if (next != cpu_wakeup_task) {
		if (next->prio < cpu_wakeup_task->prio)
			goto out_reset;

		if (next->prio == cpu_wakeup_task->prio)
			per_cpu(wakeup_sharedprio, cpu) = 1;

		goto out;
	}

	if (current->prio == cpu_wakeup_task->prio)
		per_cpu(wakeup_sharedprio, cpu) = 1;

	/*
	 * The task we are waiting for is about to be switched to.
	 * Calculate latency and store it in histogram.
	 */
	stop = ftrace_now(raw_smp_processor_id());

	latency = ((long) (stop - next->preempt_timestamp_hist)) /
	    NSECS_PER_USECS;

	if (per_cpu(wakeup_sharedprio, cpu)) {
		latency_hist(WAKEUP_LATENCY_SHAREDPRIO, cpu, latency, 0, stop,
		    next);
		per_cpu(wakeup_sharedprio, cpu) = 0;
	} else {
		latency_hist(WAKEUP_LATENCY, cpu, latency, 0, stop, next);
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
		if (timerandwakeup_enabled_data.enabled) {
			latency_hist(TIMERANDWAKEUP_LATENCY, cpu,
			    next->timer_offset + latency, next->timer_offset,
			    stop, next);
		}
#endif
	}

out_reset:
#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
	next->timer_offset = 0;
#endif
	put_task_struct(cpu_wakeup_task);
	per_cpu(wakeup_task, cpu) = NULL;
out:
	raw_spin_unlock_irqrestore(&wakeup_lock, flags);
}
#endif

#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
static notrace void probe_hrtimer_interrupt(void *v, int cpu,
	long long latency_ns, struct task_struct *curr,
	struct task_struct *task)
{
	if (latency_ns <= 0 && task != NULL && rt_task(task) &&
	    (task->prio < curr->prio ||
	    (task->prio == curr->prio &&
	    !cpumask_test_cpu(cpu, &task->cpus_allowed)))) {
		long latency;
		cycle_t now;

		if (missed_timer_offsets_pid) {
			if (likely(missed_timer_offsets_pid !=
			    task_pid_nr(task)))
				return;
		}

		now = ftrace_now(cpu);
		latency = (long) div_s64(-latency_ns, NSECS_PER_USECS);
		latency_hist(MISSED_TIMER_OFFSETS, cpu, latency, latency, now,
		    task);
#ifdef CONFIG_WAKEUP_LATENCY_HIST
		task->timer_offset = latency;
#endif
	}
}
#endif

static __init int latency_hist_init(void)
{
	struct dentry *latency_hist_root = NULL;
	struct dentry *dentry;
#ifdef CONFIG_WAKEUP_LATENCY_HIST
	struct dentry *dentry_sharedprio;
#endif
	struct dentry *entry;
	struct dentry *enable_root;
	int i = 0;
	struct hist_data *my_hist;
	char name[64];
	char *cpufmt = "CPU%d";
#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
	char *cpufmt_maxlatproc = "max_latency-CPU%d";
	struct maxlatproc_data *mp = NULL;
#endif

	dentry = tracing_init_dentry();
	latency_hist_root = debugfs_create_dir(latency_hist_dir_root, dentry);
	enable_root = debugfs_create_dir("enable", latency_hist_root);

#ifdef CONFIG_INTERRUPT_OFF_HIST
	dentry = debugfs_create_dir(irqsoff_hist_dir, latency_hist_root);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);
		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(irqsoff_hist, i), &latency_hist_fops);
		my_hist = &per_cpu(irqsoff_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;
	}
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)IRQSOFF_LATENCY, &latency_hist_reset_fops);
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
	dentry = debugfs_create_dir(preemptoff_hist_dir,
	    latency_hist_root);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);
		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(preemptoff_hist, i), &latency_hist_fops);
		my_hist = &per_cpu(preemptoff_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;
	}
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)PREEMPTOFF_LATENCY, &latency_hist_reset_fops);
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
	dentry = debugfs_create_dir(preemptirqsoff_hist_dir,
	    latency_hist_root);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);
		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(preemptirqsoff_hist, i), &latency_hist_fops);
		my_hist = &per_cpu(preemptirqsoff_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;
	}
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)PREEMPTIRQSOFF_LATENCY, &latency_hist_reset_fops);
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
	entry = debugfs_create_file("preemptirqsoff", 0644,
	    enable_root, (void *)&preemptirqsoff_enabled_data,
	    &enable_fops);
#endif

#ifdef CONFIG_WAKEUP_LATENCY_HIST
	dentry = debugfs_create_dir(wakeup_latency_hist_dir,
	    latency_hist_root);
	dentry_sharedprio = debugfs_create_dir(
	    wakeup_latency_hist_dir_sharedprio, dentry);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);

		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(wakeup_latency_hist, i),
		    &latency_hist_fops);
		my_hist = &per_cpu(wakeup_latency_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;

		entry = debugfs_create_file(name, 0444, dentry_sharedprio,
		    &per_cpu(wakeup_latency_hist_sharedprio, i),
		    &latency_hist_fops);
		my_hist = &per_cpu(wakeup_latency_hist_sharedprio, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;

		sprintf(name, cpufmt_maxlatproc, i);

		mp = &per_cpu(wakeup_maxlatproc, i);
		entry = debugfs_create_file(name, 0444, dentry, mp,
		    &maxlatproc_fops);
		clear_maxlatprocdata(mp);

		mp = &per_cpu(wakeup_maxlatproc_sharedprio, i);
		entry = debugfs_create_file(name, 0444, dentry_sharedprio, mp,
		    &maxlatproc_fops);
		clear_maxlatprocdata(mp);
	}
	entry = debugfs_create_file("pid", 0644, dentry,
	    (void *)&wakeup_pid, &pid_fops);
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)WAKEUP_LATENCY, &latency_hist_reset_fops);
	entry = debugfs_create_file("reset", 0644, dentry_sharedprio,
	    (void *)WAKEUP_LATENCY_SHAREDPRIO, &latency_hist_reset_fops);
	entry = debugfs_create_file("wakeup", 0644,
	    enable_root, (void *)&wakeup_latency_enabled_data,
	    &enable_fops);
#endif

#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
	dentry = debugfs_create_dir(missed_timer_offsets_dir,
	    latency_hist_root);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);
		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(missed_timer_offsets, i), &latency_hist_fops);
		my_hist = &per_cpu(missed_timer_offsets, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;

		sprintf(name, cpufmt_maxlatproc, i);
		mp = &per_cpu(missed_timer_offsets_maxlatproc, i);
		entry = debugfs_create_file(name, 0444, dentry, mp,
		    &maxlatproc_fops);
		clear_maxlatprocdata(mp);
	}
	entry = debugfs_create_file("pid", 0644, dentry,
	    (void *)&missed_timer_offsets_pid, &pid_fops);
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)MISSED_TIMER_OFFSETS, &latency_hist_reset_fops);
	entry = debugfs_create_file("missed_timer_offsets", 0644,
	    enable_root, (void *)&missed_timer_offsets_enabled_data,
	    &enable_fops);
#endif

#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
	dentry = debugfs_create_dir(timerandwakeup_latency_hist_dir,
	    latency_hist_root);
	for_each_possible_cpu(i) {
		sprintf(name, cpufmt, i);
		entry = debugfs_create_file(name, 0444, dentry,
		    &per_cpu(timerandwakeup_latency_hist, i),
		    &latency_hist_fops);
		my_hist = &per_cpu(timerandwakeup_latency_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = LONG_MAX;

		sprintf(name, cpufmt_maxlatproc, i);
		mp = &per_cpu(timerandwakeup_maxlatproc, i);
		entry = debugfs_create_file(name, 0444, dentry, mp,
		    &maxlatproc_fops);
		clear_maxlatprocdata(mp);
	}
	entry = debugfs_create_file("reset", 0644, dentry,
	    (void *)TIMERANDWAKEUP_LATENCY, &latency_hist_reset_fops);
	entry = debugfs_create_file("timerandwakeup", 0644,
	    enable_root, (void *)&timerandwakeup_enabled_data,
	    &enable_fops);
#endif
	return 0;
}

device_initcall(latency_hist_init);

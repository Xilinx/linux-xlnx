/*
 * builtin-timechart.c - make an svg timechart of system activity
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * Authors:
 *     Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <traceevent/event-parse.h>

#include "builtin.h"

#include "util/util.h"

#include "util/color.h"
#include <linux/list.h>
#include "util/cache.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include <linux/rbtree.h>
#include "util/symbol.h"
#include "util/callchain.h"
#include "util/strlist.h"

#include "perf.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/event.h"
#include "util/session.h"
#include "util/svghelper.h"
#include "util/tool.h"
#include "util/data.h"

#define SUPPORT_OLD_POWER_EVENTS 1
#define PWR_EVENT_EXIT -1


static unsigned int	numcpus;
static u64		min_freq;	/* Lowest CPU frequency seen */
static u64		max_freq;	/* Highest CPU frequency seen */
static u64		turbo_frequency;

static u64		first_time, last_time;

static bool		power_only;


struct per_pid;
struct per_pidcomm;

struct cpu_sample;
struct power_event;
struct wake_event;

struct sample_wrapper;

/*
 * Datastructure layout:
 * We keep an list of "pid"s, matching the kernels notion of a task struct.
 * Each "pid" entry, has a list of "comm"s.
 *	this is because we want to track different programs different, while
 *	exec will reuse the original pid (by design).
 * Each comm has a list of samples that will be used to draw
 * final graph.
 */

struct per_pid {
	struct per_pid *next;

	int		pid;
	int		ppid;

	u64		start_time;
	u64		end_time;
	u64		total_time;
	int		display;

	struct per_pidcomm *all;
	struct per_pidcomm *current;
};


struct per_pidcomm {
	struct per_pidcomm *next;

	u64		start_time;
	u64		end_time;
	u64		total_time;

	int		Y;
	int		display;

	long		state;
	u64		state_since;

	char		*comm;

	struct cpu_sample *samples;
};

struct sample_wrapper {
	struct sample_wrapper *next;

	u64		timestamp;
	unsigned char	data[0];
};

#define TYPE_NONE	0
#define TYPE_RUNNING	1
#define TYPE_WAITING	2
#define TYPE_BLOCKED	3

struct cpu_sample {
	struct cpu_sample *next;

	u64 start_time;
	u64 end_time;
	int type;
	int cpu;
};

static struct per_pid *all_data;

#define CSTATE 1
#define PSTATE 2

struct power_event {
	struct power_event *next;
	int type;
	int state;
	u64 start_time;
	u64 end_time;
	int cpu;
};

struct wake_event {
	struct wake_event *next;
	int waker;
	int wakee;
	u64 time;
};

static struct power_event    *power_events;
static struct wake_event     *wake_events;

struct process_filter;
struct process_filter {
	char			*name;
	int			pid;
	struct process_filter	*next;
};

static struct process_filter *process_filter;


static struct per_pid *find_create_pid(int pid)
{
	struct per_pid *cursor = all_data;

	while (cursor) {
		if (cursor->pid == pid)
			return cursor;
		cursor = cursor->next;
	}
	cursor = zalloc(sizeof(*cursor));
	assert(cursor != NULL);
	cursor->pid = pid;
	cursor->next = all_data;
	all_data = cursor;
	return cursor;
}

static void pid_set_comm(int pid, char *comm)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	p = find_create_pid(pid);
	c = p->all;
	while (c) {
		if (c->comm && strcmp(c->comm, comm) == 0) {
			p->current = c;
			return;
		}
		if (!c->comm) {
			c->comm = strdup(comm);
			p->current = c;
			return;
		}
		c = c->next;
	}
	c = zalloc(sizeof(*c));
	assert(c != NULL);
	c->comm = strdup(comm);
	p->current = c;
	c->next = p->all;
	p->all = c;
}

static void pid_fork(int pid, int ppid, u64 timestamp)
{
	struct per_pid *p, *pp;
	p = find_create_pid(pid);
	pp = find_create_pid(ppid);
	p->ppid = ppid;
	if (pp->current && pp->current->comm && !p->current)
		pid_set_comm(pid, pp->current->comm);

	p->start_time = timestamp;
	if (p->current) {
		p->current->start_time = timestamp;
		p->current->state_since = timestamp;
	}
}

static void pid_exit(int pid, u64 timestamp)
{
	struct per_pid *p;
	p = find_create_pid(pid);
	p->end_time = timestamp;
	if (p->current)
		p->current->end_time = timestamp;
}

static void
pid_put_sample(int pid, int type, unsigned int cpu, u64 start, u64 end)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;

	p = find_create_pid(pid);
	c = p->current;
	if (!c) {
		c = zalloc(sizeof(*c));
		assert(c != NULL);
		p->current = c;
		c->next = p->all;
		p->all = c;
	}

	sample = zalloc(sizeof(*sample));
	assert(sample != NULL);
	sample->start_time = start;
	sample->end_time = end;
	sample->type = type;
	sample->next = c->samples;
	sample->cpu = cpu;
	c->samples = sample;

	if (sample->type == TYPE_RUNNING && end > start && start > 0) {
		c->total_time += (end-start);
		p->total_time += (end-start);
	}

	if (c->start_time == 0 || c->start_time > start)
		c->start_time = start;
	if (p->start_time == 0 || p->start_time > start)
		p->start_time = start;
}

#define MAX_CPUS 4096

static u64 cpus_cstate_start_times[MAX_CPUS];
static int cpus_cstate_state[MAX_CPUS];
static u64 cpus_pstate_start_times[MAX_CPUS];
static u64 cpus_pstate_state[MAX_CPUS];

static int process_comm_event(struct perf_tool *tool __maybe_unused,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	pid_set_comm(event->comm.tid, event->comm.comm);
	return 0;
}

static int process_fork_event(struct perf_tool *tool __maybe_unused,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	pid_fork(event->fork.pid, event->fork.ppid, event->fork.time);
	return 0;
}

static int process_exit_event(struct perf_tool *tool __maybe_unused,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	pid_exit(event->fork.pid, event->fork.time);
	return 0;
}

struct trace_entry {
	unsigned short		type;
	unsigned char		flags;
	unsigned char		preempt_count;
	int			pid;
	int			lock_depth;
};

#ifdef SUPPORT_OLD_POWER_EVENTS
static int use_old_power_events;
struct power_entry_old {
	struct trace_entry te;
	u64	type;
	u64	value;
	u64	cpu_id;
};
#endif

struct power_processor_entry {
	struct trace_entry te;
	u32	state;
	u32	cpu_id;
};

#define TASK_COMM_LEN 16
struct wakeup_entry {
	struct trace_entry te;
	char comm[TASK_COMM_LEN];
	int   pid;
	int   prio;
	int   success;
};

struct sched_switch {
	struct trace_entry te;
	char prev_comm[TASK_COMM_LEN];
	int  prev_pid;
	int  prev_prio;
	long prev_state; /* Arjan weeps. */
	char next_comm[TASK_COMM_LEN];
	int  next_pid;
	int  next_prio;
};

static void c_state_start(int cpu, u64 timestamp, int state)
{
	cpus_cstate_start_times[cpu] = timestamp;
	cpus_cstate_state[cpu] = state;
}

static void c_state_end(int cpu, u64 timestamp)
{
	struct power_event *pwr = zalloc(sizeof(*pwr));

	if (!pwr)
		return;

	pwr->state = cpus_cstate_state[cpu];
	pwr->start_time = cpus_cstate_start_times[cpu];
	pwr->end_time = timestamp;
	pwr->cpu = cpu;
	pwr->type = CSTATE;
	pwr->next = power_events;

	power_events = pwr;
}

static void p_state_change(int cpu, u64 timestamp, u64 new_freq)
{
	struct power_event *pwr;

	if (new_freq > 8000000) /* detect invalid data */
		return;

	pwr = zalloc(sizeof(*pwr));
	if (!pwr)
		return;

	pwr->state = cpus_pstate_state[cpu];
	pwr->start_time = cpus_pstate_start_times[cpu];
	pwr->end_time = timestamp;
	pwr->cpu = cpu;
	pwr->type = PSTATE;
	pwr->next = power_events;

	if (!pwr->start_time)
		pwr->start_time = first_time;

	power_events = pwr;

	cpus_pstate_state[cpu] = new_freq;
	cpus_pstate_start_times[cpu] = timestamp;

	if ((u64)new_freq > max_freq)
		max_freq = new_freq;

	if (new_freq < min_freq || min_freq == 0)
		min_freq = new_freq;

	if (new_freq == max_freq - 1000)
			turbo_frequency = max_freq;
}

static void
sched_wakeup(int cpu, u64 timestamp, int pid, struct trace_entry *te)
{
	struct per_pid *p;
	struct wakeup_entry *wake = (void *)te;
	struct wake_event *we = zalloc(sizeof(*we));

	if (!we)
		return;

	we->time = timestamp;
	we->waker = pid;

	if ((te->flags & TRACE_FLAG_HARDIRQ) || (te->flags & TRACE_FLAG_SOFTIRQ))
		we->waker = -1;

	we->wakee = wake->pid;
	we->next = wake_events;
	wake_events = we;
	p = find_create_pid(we->wakee);

	if (p && p->current && p->current->state == TYPE_NONE) {
		p->current->state_since = timestamp;
		p->current->state = TYPE_WAITING;
	}
	if (p && p->current && p->current->state == TYPE_BLOCKED) {
		pid_put_sample(p->pid, p->current->state, cpu, p->current->state_since, timestamp);
		p->current->state_since = timestamp;
		p->current->state = TYPE_WAITING;
	}
}

static void sched_switch(int cpu, u64 timestamp, struct trace_entry *te)
{
	struct per_pid *p = NULL, *prev_p;
	struct sched_switch *sw = (void *)te;


	prev_p = find_create_pid(sw->prev_pid);

	p = find_create_pid(sw->next_pid);

	if (prev_p->current && prev_p->current->state != TYPE_NONE)
		pid_put_sample(sw->prev_pid, TYPE_RUNNING, cpu, prev_p->current->state_since, timestamp);
	if (p && p->current) {
		if (p->current->state != TYPE_NONE)
			pid_put_sample(sw->next_pid, p->current->state, cpu, p->current->state_since, timestamp);

		p->current->state_since = timestamp;
		p->current->state = TYPE_RUNNING;
	}

	if (prev_p->current) {
		prev_p->current->state = TYPE_NONE;
		prev_p->current->state_since = timestamp;
		if (sw->prev_state & 2)
			prev_p->current->state = TYPE_BLOCKED;
		if (sw->prev_state == 0)
			prev_p->current->state = TYPE_WAITING;
	}
}

typedef int (*tracepoint_handler)(struct perf_evsel *evsel,
				  struct perf_sample *sample);

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine __maybe_unused)
{
	if (evsel->attr.sample_type & PERF_SAMPLE_TIME) {
		if (!first_time || first_time > sample->time)
			first_time = sample->time;
		if (last_time < sample->time)
			last_time = sample->time;
	}

	if (sample->cpu > numcpus)
		numcpus = sample->cpu;

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		return f(evsel, sample);
	}

	return 0;
}

static int
process_sample_cpu_idle(struct perf_evsel *evsel __maybe_unused,
			struct perf_sample *sample)
{
	struct power_processor_entry *ppe = sample->raw_data;

	if (ppe->state == (u32) PWR_EVENT_EXIT)
		c_state_end(ppe->cpu_id, sample->time);
	else
		c_state_start(ppe->cpu_id, sample->time, ppe->state);
	return 0;
}

static int
process_sample_cpu_frequency(struct perf_evsel *evsel __maybe_unused,
			     struct perf_sample *sample)
{
	struct power_processor_entry *ppe = sample->raw_data;

	p_state_change(ppe->cpu_id, sample->time, ppe->state);
	return 0;
}

static int
process_sample_sched_wakeup(struct perf_evsel *evsel __maybe_unused,
			    struct perf_sample *sample)
{
	struct trace_entry *te = sample->raw_data;

	sched_wakeup(sample->cpu, sample->time, sample->pid, te);
	return 0;
}

static int
process_sample_sched_switch(struct perf_evsel *evsel __maybe_unused,
			    struct perf_sample *sample)
{
	struct trace_entry *te = sample->raw_data;

	sched_switch(sample->cpu, sample->time, te);
	return 0;
}

#ifdef SUPPORT_OLD_POWER_EVENTS
static int
process_sample_power_start(struct perf_evsel *evsel __maybe_unused,
			   struct perf_sample *sample)
{
	struct power_entry_old *peo = sample->raw_data;

	c_state_start(peo->cpu_id, sample->time, peo->value);
	return 0;
}

static int
process_sample_power_end(struct perf_evsel *evsel __maybe_unused,
			 struct perf_sample *sample)
{
	c_state_end(sample->cpu, sample->time);
	return 0;
}

static int
process_sample_power_frequency(struct perf_evsel *evsel __maybe_unused,
			       struct perf_sample *sample)
{
	struct power_entry_old *peo = sample->raw_data;

	p_state_change(peo->cpu_id, sample->time, peo->value);
	return 0;
}
#endif /* SUPPORT_OLD_POWER_EVENTS */

/*
 * After the last sample we need to wrap up the current C/P state
 * and close out each CPU for these.
 */
static void end_sample_processing(void)
{
	u64 cpu;
	struct power_event *pwr;

	for (cpu = 0; cpu <= numcpus; cpu++) {
		/* C state */
#if 0
		pwr = zalloc(sizeof(*pwr));
		if (!pwr)
			return;

		pwr->state = cpus_cstate_state[cpu];
		pwr->start_time = cpus_cstate_start_times[cpu];
		pwr->end_time = last_time;
		pwr->cpu = cpu;
		pwr->type = CSTATE;
		pwr->next = power_events;

		power_events = pwr;
#endif
		/* P state */

		pwr = zalloc(sizeof(*pwr));
		if (!pwr)
			return;

		pwr->state = cpus_pstate_state[cpu];
		pwr->start_time = cpus_pstate_start_times[cpu];
		pwr->end_time = last_time;
		pwr->cpu = cpu;
		pwr->type = PSTATE;
		pwr->next = power_events;

		if (!pwr->start_time)
			pwr->start_time = first_time;
		if (!pwr->state)
			pwr->state = min_freq;
		power_events = pwr;
	}
}

/*
 * Sort the pid datastructure
 */
static void sort_pids(void)
{
	struct per_pid *new_list, *p, *cursor, *prev;
	/* sort by ppid first, then by pid, lowest to highest */

	new_list = NULL;

	while (all_data) {
		p = all_data;
		all_data = p->next;
		p->next = NULL;

		if (new_list == NULL) {
			new_list = p;
			p->next = NULL;
			continue;
		}
		prev = NULL;
		cursor = new_list;
		while (cursor) {
			if (cursor->ppid > p->ppid ||
				(cursor->ppid == p->ppid && cursor->pid > p->pid)) {
				/* must insert before */
				if (prev) {
					p->next = prev->next;
					prev->next = p;
					cursor = NULL;
					continue;
				} else {
					p->next = new_list;
					new_list = p;
					cursor = NULL;
					continue;
				}
			}

			prev = cursor;
			cursor = cursor->next;
			if (!cursor)
				prev->next = p;
		}
	}
	all_data = new_list;
}


static void draw_c_p_states(void)
{
	struct power_event *pwr;
	pwr = power_events;

	/*
	 * two pass drawing so that the P state bars are on top of the C state blocks
	 */
	while (pwr) {
		if (pwr->type == CSTATE)
			svg_cstate(pwr->cpu, pwr->start_time, pwr->end_time, pwr->state);
		pwr = pwr->next;
	}

	pwr = power_events;
	while (pwr) {
		if (pwr->type == PSTATE) {
			if (!pwr->state)
				pwr->state = min_freq;
			svg_pstate(pwr->cpu, pwr->start_time, pwr->end_time, pwr->state);
		}
		pwr = pwr->next;
	}
}

static void draw_wakeups(void)
{
	struct wake_event *we;
	struct per_pid *p;
	struct per_pidcomm *c;

	we = wake_events;
	while (we) {
		int from = 0, to = 0;
		char *task_from = NULL, *task_to = NULL;

		/* locate the column of the waker and wakee */
		p = all_data;
		while (p) {
			if (p->pid == we->waker || p->pid == we->wakee) {
				c = p->all;
				while (c) {
					if (c->Y && c->start_time <= we->time && c->end_time >= we->time) {
						if (p->pid == we->waker && !from) {
							from = c->Y;
							task_from = strdup(c->comm);
						}
						if (p->pid == we->wakee && !to) {
							to = c->Y;
							task_to = strdup(c->comm);
						}
					}
					c = c->next;
				}
				c = p->all;
				while (c) {
					if (p->pid == we->waker && !from) {
						from = c->Y;
						task_from = strdup(c->comm);
					}
					if (p->pid == we->wakee && !to) {
						to = c->Y;
						task_to = strdup(c->comm);
					}
					c = c->next;
				}
			}
			p = p->next;
		}

		if (!task_from) {
			task_from = malloc(40);
			sprintf(task_from, "[%i]", we->waker);
		}
		if (!task_to) {
			task_to = malloc(40);
			sprintf(task_to, "[%i]", we->wakee);
		}

		if (we->waker == -1)
			svg_interrupt(we->time, to);
		else if (from && to && abs(from - to) == 1)
			svg_wakeline(we->time, from, to);
		else
			svg_partial_wakeline(we->time, from, task_from, to, task_to);
		we = we->next;

		free(task_from);
		free(task_to);
	}
}

static void draw_cpu_usage(void)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;
	p = all_data;
	while (p) {
		c = p->all;
		while (c) {
			sample = c->samples;
			while (sample) {
				if (sample->type == TYPE_RUNNING)
					svg_process(sample->cpu, sample->start_time, sample->end_time, "sample", c->comm);

				sample = sample->next;
			}
			c = c->next;
		}
		p = p->next;
	}
}

static void draw_process_bars(void)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;
	int Y = 0;

	Y = 2 * numcpus + 2;

	p = all_data;
	while (p) {
		c = p->all;
		while (c) {
			if (!c->display) {
				c->Y = 0;
				c = c->next;
				continue;
			}

			svg_box(Y, c->start_time, c->end_time, "process");
			sample = c->samples;
			while (sample) {
				if (sample->type == TYPE_RUNNING)
					svg_sample(Y, sample->cpu, sample->start_time, sample->end_time);
				if (sample->type == TYPE_BLOCKED)
					svg_box(Y, sample->start_time, sample->end_time, "blocked");
				if (sample->type == TYPE_WAITING)
					svg_waiting(Y, sample->start_time, sample->end_time);
				sample = sample->next;
			}

			if (c->comm) {
				char comm[256];
				if (c->total_time > 5000000000) /* 5 seconds */
					sprintf(comm, "%s:%i (%2.2fs)", c->comm, p->pid, c->total_time / 1000000000.0);
				else
					sprintf(comm, "%s:%i (%3.1fms)", c->comm, p->pid, c->total_time / 1000000.0);

				svg_text(Y, c->start_time, comm);
			}
			c->Y = Y;
			Y++;
			c = c->next;
		}
		p = p->next;
	}
}

static void add_process_filter(const char *string)
{
	int pid = strtoull(string, NULL, 10);
	struct process_filter *filt = malloc(sizeof(*filt));

	if (!filt)
		return;

	filt->name = strdup(string);
	filt->pid  = pid;
	filt->next = process_filter;

	process_filter = filt;
}

static int passes_filter(struct per_pid *p, struct per_pidcomm *c)
{
	struct process_filter *filt;
	if (!process_filter)
		return 1;

	filt = process_filter;
	while (filt) {
		if (filt->pid && p->pid == filt->pid)
			return 1;
		if (strcmp(filt->name, c->comm) == 0)
			return 1;
		filt = filt->next;
	}
	return 0;
}

static int determine_display_tasks_filtered(void)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	int count = 0;

	p = all_data;
	while (p) {
		p->display = 0;
		if (p->start_time == 1)
			p->start_time = first_time;

		/* no exit marker, task kept running to the end */
		if (p->end_time == 0)
			p->end_time = last_time;

		c = p->all;

		while (c) {
			c->display = 0;

			if (c->start_time == 1)
				c->start_time = first_time;

			if (passes_filter(p, c)) {
				c->display = 1;
				p->display = 1;
				count++;
			}

			if (c->end_time == 0)
				c->end_time = last_time;

			c = c->next;
		}
		p = p->next;
	}
	return count;
}

static int determine_display_tasks(u64 threshold)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	int count = 0;

	if (process_filter)
		return determine_display_tasks_filtered();

	p = all_data;
	while (p) {
		p->display = 0;
		if (p->start_time == 1)
			p->start_time = first_time;

		/* no exit marker, task kept running to the end */
		if (p->end_time == 0)
			p->end_time = last_time;
		if (p->total_time >= threshold && !power_only)
			p->display = 1;

		c = p->all;

		while (c) {
			c->display = 0;

			if (c->start_time == 1)
				c->start_time = first_time;

			if (c->total_time >= threshold && !power_only) {
				c->display = 1;
				count++;
			}

			if (c->end_time == 0)
				c->end_time = last_time;

			c = c->next;
		}
		p = p->next;
	}
	return count;
}



#define TIME_THRESH 10000000

static void write_svg_file(const char *filename)
{
	u64 i;
	int count;

	numcpus++;


	count = determine_display_tasks(TIME_THRESH);

	/* We'd like to show at least 15 tasks; be less picky if we have fewer */
	if (count < 15)
		count = determine_display_tasks(TIME_THRESH / 10);

	open_svg(filename, numcpus, count, first_time, last_time);

	svg_time_grid();
	svg_legenda();

	for (i = 0; i < numcpus; i++)
		svg_cpu_box(i, max_freq, turbo_frequency);

	draw_cpu_usage();
	draw_process_bars();
	draw_c_p_states();
	draw_wakeups();

	svg_close();
}

static int __cmd_timechart(const char *output_name)
{
	struct perf_tool perf_timechart = {
		.comm		 = process_comm_event,
		.fork		 = process_fork_event,
		.exit		 = process_exit_event,
		.sample		 = process_sample_event,
		.ordered_samples = true,
	};
	const struct perf_evsel_str_handler power_tracepoints[] = {
		{ "power:cpu_idle",		process_sample_cpu_idle },
		{ "power:cpu_frequency",	process_sample_cpu_frequency },
		{ "sched:sched_wakeup",		process_sample_sched_wakeup },
		{ "sched:sched_switch",		process_sample_sched_switch },
#ifdef SUPPORT_OLD_POWER_EVENTS
		{ "power:power_start",		process_sample_power_start },
		{ "power:power_end",		process_sample_power_end },
		{ "power:power_frequency",	process_sample_power_frequency },
#endif
	};
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
	};

	struct perf_session *session = perf_session__new(&file, false,
							 &perf_timechart);
	int ret = -EINVAL;

	if (session == NULL)
		return -ENOMEM;

	if (!perf_session__has_traces(session, "timechart record"))
		goto out_delete;

	if (perf_session__set_tracepoints_handlers(session,
						   power_tracepoints)) {
		pr_err("Initializing session tracepoint handlers failed\n");
		goto out_delete;
	}

	ret = perf_session__process_events(session, &perf_timechart);
	if (ret)
		goto out_delete;

	end_sample_processing();

	sort_pids();

	write_svg_file(output_name);

	pr_info("Written %2.1f seconds of trace to %s.\n",
		(last_time - first_time) / 1000000000.0, output_name);
out_delete:
	perf_session__delete(session);
	return ret;
}

static int __cmd_record(int argc, const char **argv)
{
#ifdef SUPPORT_OLD_POWER_EVENTS
	const char * const record_old_args[] = {
		"record", "-a", "-R", "-c", "1",
		"-e", "power:power_start",
		"-e", "power:power_end",
		"-e", "power:power_frequency",
		"-e", "sched:sched_wakeup",
		"-e", "sched:sched_switch",
	};
#endif
	const char * const record_new_args[] = {
		"record", "-a", "-R", "-c", "1",
		"-e", "power:cpu_frequency",
		"-e", "power:cpu_idle",
		"-e", "sched:sched_wakeup",
		"-e", "sched:sched_switch",
	};
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char * const *record_args = record_new_args;
	unsigned int record_elems = ARRAY_SIZE(record_new_args);

#ifdef SUPPORT_OLD_POWER_EVENTS
	if (!is_valid_tracepoint("power:cpu_idle") &&
	    is_valid_tracepoint("power:power_start")) {
		use_old_power_events = 1;
		record_args = record_old_args;
		record_elems = ARRAY_SIZE(record_old_args);
	}
#endif

	rec_argc = record_elems + argc - 1;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < record_elems; i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv, NULL);
}

static int
parse_process(const struct option *opt __maybe_unused, const char *arg,
	      int __maybe_unused unset)
{
	if (arg)
		add_process_filter(arg);
	return 0;
}

int cmd_timechart(int argc, const char **argv,
		  const char *prefix __maybe_unused)
{
	const char *output_name = "output.svg";
	const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_INTEGER('w', "width", &svg_page_width, "page width"),
	OPT_BOOLEAN('P', "power-only", &power_only, "output power data only"),
	OPT_CALLBACK('p', "process", NULL, "process",
		      "process selector. Pass a pid or process name.",
		       parse_process),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		    "Look for files with symbols relative to this directory"),
	OPT_END()
	};
	const char * const timechart_usage[] = {
		"perf timechart [<options>] {record}",
		NULL
	};

	argc = parse_options(argc, argv, options, timechart_usage,
			PARSE_OPT_STOP_AT_NON_OPTION);

	symbol__init();

	if (argc && !strncmp(argv[0], "rec", 3))
		return __cmd_record(argc, argv);
	else if (argc)
		usage_with_options(timechart_usage, options);

	setup_pager();

	return __cmd_timechart(output_name);
}

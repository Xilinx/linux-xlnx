/*
 * intel_pt.c: Intel Processor Trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "../perf.h"
#include "session.h"
#include "machine.h"
#include "sort.h"
#include "tool.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "map.h"
#include "color.h"
#include "util.h"
#include "thread.h"
#include "thread-stack.h"
#include "symbol.h"
#include "callchain.h"
#include "dso.h"
#include "debug.h"
#include "auxtrace.h"
#include "tsc.h"
#include "intel-pt.h"
#include "config.h"

#include "intel-pt-decoder/intel-pt-log.h"
#include "intel-pt-decoder/intel-pt-decoder.h"
#include "intel-pt-decoder/intel-pt-insn-decoder.h"
#include "intel-pt-decoder/intel-pt-pkt-decoder.h"

#define MAX_TIMESTAMP (~0ULL)

struct intel_pt {
	struct auxtrace auxtrace;
	struct auxtrace_queues queues;
	struct auxtrace_heap heap;
	u32 auxtrace_type;
	struct perf_session *session;
	struct machine *machine;
	struct perf_evsel *switch_evsel;
	struct thread *unknown_thread;
	bool timeless_decoding;
	bool sampling_mode;
	bool snapshot_mode;
	bool per_cpu_mmaps;
	bool have_tsc;
	bool data_queued;
	bool est_tsc;
	bool sync_switch;
	bool mispred_all;
	int have_sched_switch;
	u32 pmu_type;
	u64 kernel_start;
	u64 switch_ip;
	u64 ptss_ip;

	struct perf_tsc_conversion tc;
	bool cap_user_time_zero;

	struct itrace_synth_opts synth_opts;

	bool sample_instructions;
	u64 instructions_sample_type;
	u64 instructions_sample_period;
	u64 instructions_id;

	bool sample_branches;
	u32 branches_filter;
	u64 branches_sample_type;
	u64 branches_id;

	bool sample_transactions;
	u64 transactions_sample_type;
	u64 transactions_id;

	bool synth_needs_swap;

	u64 tsc_bit;
	u64 mtc_bit;
	u64 mtc_freq_bits;
	u32 tsc_ctc_ratio_n;
	u32 tsc_ctc_ratio_d;
	u64 cyc_bit;
	u64 noretcomp_bit;
	unsigned max_non_turbo_ratio;

	unsigned long num_events;

	char *filter;
	struct addr_filters filts;
};

enum switch_state {
	INTEL_PT_SS_NOT_TRACING,
	INTEL_PT_SS_UNKNOWN,
	INTEL_PT_SS_TRACING,
	INTEL_PT_SS_EXPECTING_SWITCH_EVENT,
	INTEL_PT_SS_EXPECTING_SWITCH_IP,
};

struct intel_pt_queue {
	struct intel_pt *pt;
	unsigned int queue_nr;
	struct auxtrace_buffer *buffer;
	void *decoder;
	const struct intel_pt_state *state;
	struct ip_callchain *chain;
	struct branch_stack *last_branch;
	struct branch_stack *last_branch_rb;
	size_t last_branch_pos;
	union perf_event *event_buf;
	bool on_heap;
	bool stop;
	bool step_through_buffers;
	bool use_buffer_pid_tid;
	pid_t pid, tid;
	int cpu;
	int switch_state;
	pid_t next_tid;
	struct thread *thread;
	bool exclude_kernel;
	bool have_sample;
	u64 time;
	u64 timestamp;
	u32 flags;
	u16 insn_len;
	u64 last_insn_cnt;
};

static void intel_pt_dump(struct intel_pt *pt __maybe_unused,
			  unsigned char *buf, size_t len)
{
	struct intel_pt_pkt packet;
	size_t pos = 0;
	int ret, pkt_len, i;
	char desc[INTEL_PT_PKT_DESC_MAX];
	const char *color = PERF_COLOR_BLUE;

	color_fprintf(stdout, color,
		      ". ... Intel Processor Trace data: size %zu bytes\n",
		      len);

	while (len) {
		ret = intel_pt_get_packet(buf, len, &packet);
		if (ret > 0)
			pkt_len = ret;
		else
			pkt_len = 1;
		printf(".");
		color_fprintf(stdout, color, "  %08x: ", pos);
		for (i = 0; i < pkt_len; i++)
			color_fprintf(stdout, color, " %02x", buf[i]);
		for (; i < 16; i++)
			color_fprintf(stdout, color, "   ");
		if (ret > 0) {
			ret = intel_pt_pkt_desc(&packet, desc,
						INTEL_PT_PKT_DESC_MAX);
			if (ret > 0)
				color_fprintf(stdout, color, " %s\n", desc);
		} else {
			color_fprintf(stdout, color, " Bad packet!\n");
		}
		pos += pkt_len;
		buf += pkt_len;
		len -= pkt_len;
	}
}

static void intel_pt_dump_event(struct intel_pt *pt, unsigned char *buf,
				size_t len)
{
	printf(".\n");
	intel_pt_dump(pt, buf, len);
}

static int intel_pt_do_fix_overlap(struct intel_pt *pt, struct auxtrace_buffer *a,
				   struct auxtrace_buffer *b)
{
	void *start;

	start = intel_pt_find_overlap(a->data, a->size, b->data, b->size,
				      pt->have_tsc);
	if (!start)
		return -EINVAL;
	b->use_size = b->data + b->size - start;
	b->use_data = start;
	return 0;
}

static void intel_pt_use_buffer_pid_tid(struct intel_pt_queue *ptq,
					struct auxtrace_queue *queue,
					struct auxtrace_buffer *buffer)
{
	if (queue->cpu == -1 && buffer->cpu != -1)
		ptq->cpu = buffer->cpu;

	ptq->pid = buffer->pid;
	ptq->tid = buffer->tid;

	intel_pt_log("queue %u cpu %d pid %d tid %d\n",
		     ptq->queue_nr, ptq->cpu, ptq->pid, ptq->tid);

	thread__zput(ptq->thread);

	if (ptq->tid != -1) {
		if (ptq->pid != -1)
			ptq->thread = machine__findnew_thread(ptq->pt->machine,
							      ptq->pid,
							      ptq->tid);
		else
			ptq->thread = machine__find_thread(ptq->pt->machine, -1,
							   ptq->tid);
	}
}

/* This function assumes data is processed sequentially only */
static int intel_pt_get_trace(struct intel_pt_buffer *b, void *data)
{
	struct intel_pt_queue *ptq = data;
	struct auxtrace_buffer *buffer = ptq->buffer, *old_buffer = buffer;
	struct auxtrace_queue *queue;

	if (ptq->stop) {
		b->len = 0;
		return 0;
	}

	queue = &ptq->pt->queues.queue_array[ptq->queue_nr];
next:
	buffer = auxtrace_buffer__next(queue, buffer);
	if (!buffer) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		b->len = 0;
		return 0;
	}

	ptq->buffer = buffer;

	if (!buffer->data) {
		int fd = perf_data_file__fd(ptq->pt->session->file);

		buffer->data = auxtrace_buffer__get_data(buffer, fd);
		if (!buffer->data)
			return -ENOMEM;
	}

	if (ptq->pt->snapshot_mode && !buffer->consecutive && old_buffer &&
	    intel_pt_do_fix_overlap(ptq->pt, old_buffer, buffer))
		return -ENOMEM;

	if (buffer->use_data) {
		b->len = buffer->use_size;
		b->buf = buffer->use_data;
	} else {
		b->len = buffer->size;
		b->buf = buffer->data;
	}
	b->ref_timestamp = buffer->reference;

	/*
	 * If in snapshot mode and the buffer has no usable data, get next
	 * buffer and again check overlap against old_buffer.
	 */
	if (ptq->pt->snapshot_mode && !b->len)
		goto next;

	if (old_buffer)
		auxtrace_buffer__drop_data(old_buffer);

	if (!old_buffer || ptq->pt->sampling_mode || (ptq->pt->snapshot_mode &&
						      !buffer->consecutive)) {
		b->consecutive = false;
		b->trace_nr = buffer->buffer_nr + 1;
	} else {
		b->consecutive = true;
	}

	if (ptq->use_buffer_pid_tid && (ptq->pid != buffer->pid ||
					ptq->tid != buffer->tid))
		intel_pt_use_buffer_pid_tid(ptq, queue, buffer);

	if (ptq->step_through_buffers)
		ptq->stop = true;

	if (!b->len)
		return intel_pt_get_trace(b, data);

	return 0;
}

struct intel_pt_cache_entry {
	struct auxtrace_cache_entry	entry;
	u64				insn_cnt;
	u64				byte_cnt;
	enum intel_pt_insn_op		op;
	enum intel_pt_insn_branch	branch;
	int				length;
	int32_t				rel;
};

static int intel_pt_config_div(const char *var, const char *value, void *data)
{
	int *d = data;
	long val;

	if (!strcmp(var, "intel-pt.cache-divisor")) {
		val = strtol(value, NULL, 0);
		if (val > 0 && val <= INT_MAX)
			*d = val;
	}

	return 0;
}

static int intel_pt_cache_divisor(void)
{
	static int d;

	if (d)
		return d;

	perf_config(intel_pt_config_div, &d);

	if (!d)
		d = 64;

	return d;
}

static unsigned int intel_pt_cache_size(struct dso *dso,
					struct machine *machine)
{
	off_t size;

	size = dso__data_size(dso, machine);
	size /= intel_pt_cache_divisor();
	if (size < 1000)
		return 10;
	if (size > (1 << 21))
		return 21;
	return 32 - __builtin_clz(size);
}

static struct auxtrace_cache *intel_pt_cache(struct dso *dso,
					     struct machine *machine)
{
	struct auxtrace_cache *c;
	unsigned int bits;

	if (dso->auxtrace_cache)
		return dso->auxtrace_cache;

	bits = intel_pt_cache_size(dso, machine);

	/* Ignoring cache creation failure */
	c = auxtrace_cache__new(bits, sizeof(struct intel_pt_cache_entry), 200);

	dso->auxtrace_cache = c;

	return c;
}

static int intel_pt_cache_add(struct dso *dso, struct machine *machine,
			      u64 offset, u64 insn_cnt, u64 byte_cnt,
			      struct intel_pt_insn *intel_pt_insn)
{
	struct auxtrace_cache *c = intel_pt_cache(dso, machine);
	struct intel_pt_cache_entry *e;
	int err;

	if (!c)
		return -ENOMEM;

	e = auxtrace_cache__alloc_entry(c);
	if (!e)
		return -ENOMEM;

	e->insn_cnt = insn_cnt;
	e->byte_cnt = byte_cnt;
	e->op = intel_pt_insn->op;
	e->branch = intel_pt_insn->branch;
	e->length = intel_pt_insn->length;
	e->rel = intel_pt_insn->rel;

	err = auxtrace_cache__add(c, offset, &e->entry);
	if (err)
		auxtrace_cache__free_entry(c, e);

	return err;
}

static struct intel_pt_cache_entry *
intel_pt_cache_lookup(struct dso *dso, struct machine *machine, u64 offset)
{
	struct auxtrace_cache *c = intel_pt_cache(dso, machine);

	if (!c)
		return NULL;

	return auxtrace_cache__lookup(dso->auxtrace_cache, offset);
}

static int intel_pt_walk_next_insn(struct intel_pt_insn *intel_pt_insn,
				   uint64_t *insn_cnt_ptr, uint64_t *ip,
				   uint64_t to_ip, uint64_t max_insn_cnt,
				   void *data)
{
	struct intel_pt_queue *ptq = data;
	struct machine *machine = ptq->pt->machine;
	struct thread *thread;
	struct addr_location al;
	unsigned char buf[1024];
	size_t bufsz;
	ssize_t len;
	int x86_64;
	u8 cpumode;
	u64 offset, start_offset, start_ip;
	u64 insn_cnt = 0;
	bool one_map = true;

	if (to_ip && *ip == to_ip)
		goto out_no_cache;

	bufsz = intel_pt_insn_max_size();

	if (*ip >= ptq->pt->kernel_start)
		cpumode = PERF_RECORD_MISC_KERNEL;
	else
		cpumode = PERF_RECORD_MISC_USER;

	thread = ptq->thread;
	if (!thread) {
		if (cpumode != PERF_RECORD_MISC_KERNEL)
			return -EINVAL;
		thread = ptq->pt->unknown_thread;
	}

	while (1) {
		thread__find_addr_map(thread, cpumode, MAP__FUNCTION, *ip, &al);
		if (!al.map || !al.map->dso)
			return -EINVAL;

		if (al.map->dso->data.status == DSO_DATA_STATUS_ERROR &&
		    dso__data_status_seen(al.map->dso,
					  DSO_DATA_STATUS_SEEN_ITRACE))
			return -ENOENT;

		offset = al.map->map_ip(al.map, *ip);

		if (!to_ip && one_map) {
			struct intel_pt_cache_entry *e;

			e = intel_pt_cache_lookup(al.map->dso, machine, offset);
			if (e &&
			    (!max_insn_cnt || e->insn_cnt <= max_insn_cnt)) {
				*insn_cnt_ptr = e->insn_cnt;
				*ip += e->byte_cnt;
				intel_pt_insn->op = e->op;
				intel_pt_insn->branch = e->branch;
				intel_pt_insn->length = e->length;
				intel_pt_insn->rel = e->rel;
				intel_pt_log_insn_no_data(intel_pt_insn, *ip);
				return 0;
			}
		}

		start_offset = offset;
		start_ip = *ip;

		/* Load maps to ensure dso->is_64_bit has been updated */
		map__load(al.map);

		x86_64 = al.map->dso->is_64_bit;

		while (1) {
			len = dso__data_read_offset(al.map->dso, machine,
						    offset, buf, bufsz);
			if (len <= 0)
				return -EINVAL;

			if (intel_pt_get_insn(buf, len, x86_64, intel_pt_insn))
				return -EINVAL;

			intel_pt_log_insn(intel_pt_insn, *ip);

			insn_cnt += 1;

			if (intel_pt_insn->branch != INTEL_PT_BR_NO_BRANCH)
				goto out;

			if (max_insn_cnt && insn_cnt >= max_insn_cnt)
				goto out_no_cache;

			*ip += intel_pt_insn->length;

			if (to_ip && *ip == to_ip)
				goto out_no_cache;

			if (*ip >= al.map->end)
				break;

			offset += intel_pt_insn->length;
		}
		one_map = false;
	}
out:
	*insn_cnt_ptr = insn_cnt;

	if (!one_map)
		goto out_no_cache;

	/*
	 * Didn't lookup in the 'to_ip' case, so do it now to prevent duplicate
	 * entries.
	 */
	if (to_ip) {
		struct intel_pt_cache_entry *e;

		e = intel_pt_cache_lookup(al.map->dso, machine, start_offset);
		if (e)
			return 0;
	}

	/* Ignore cache errors */
	intel_pt_cache_add(al.map->dso, machine, start_offset, insn_cnt,
			   *ip - start_ip, intel_pt_insn);

	return 0;

out_no_cache:
	*insn_cnt_ptr = insn_cnt;
	return 0;
}

static bool intel_pt_match_pgd_ip(struct intel_pt *pt, uint64_t ip,
				  uint64_t offset, const char *filename)
{
	struct addr_filter *filt;
	bool have_filter   = false;
	bool hit_tracestop = false;
	bool hit_filter    = false;

	list_for_each_entry(filt, &pt->filts.head, list) {
		if (filt->start)
			have_filter = true;

		if ((filename && !filt->filename) ||
		    (!filename && filt->filename) ||
		    (filename && strcmp(filename, filt->filename)))
			continue;

		if (!(offset >= filt->addr && offset < filt->addr + filt->size))
			continue;

		intel_pt_log("TIP.PGD ip %#"PRIx64" offset %#"PRIx64" in %s hit filter: %s offset %#"PRIx64" size %#"PRIx64"\n",
			     ip, offset, filename ? filename : "[kernel]",
			     filt->start ? "filter" : "stop",
			     filt->addr, filt->size);

		if (filt->start)
			hit_filter = true;
		else
			hit_tracestop = true;
	}

	if (!hit_tracestop && !hit_filter)
		intel_pt_log("TIP.PGD ip %#"PRIx64" offset %#"PRIx64" in %s is not in a filter region\n",
			     ip, offset, filename ? filename : "[kernel]");

	return hit_tracestop || (have_filter && !hit_filter);
}

static int __intel_pt_pgd_ip(uint64_t ip, void *data)
{
	struct intel_pt_queue *ptq = data;
	struct thread *thread;
	struct addr_location al;
	u8 cpumode;
	u64 offset;

	if (ip >= ptq->pt->kernel_start)
		return intel_pt_match_pgd_ip(ptq->pt, ip, ip, NULL);

	cpumode = PERF_RECORD_MISC_USER;

	thread = ptq->thread;
	if (!thread)
		return -EINVAL;

	thread__find_addr_map(thread, cpumode, MAP__FUNCTION, ip, &al);
	if (!al.map || !al.map->dso)
		return -EINVAL;

	offset = al.map->map_ip(al.map, ip);

	return intel_pt_match_pgd_ip(ptq->pt, ip, offset,
				     al.map->dso->long_name);
}

static bool intel_pt_pgd_ip(uint64_t ip, void *data)
{
	return __intel_pt_pgd_ip(ip, data) > 0;
}

static bool intel_pt_get_config(struct intel_pt *pt,
				struct perf_event_attr *attr, u64 *config)
{
	if (attr->type == pt->pmu_type) {
		if (config)
			*config = attr->config;
		return true;
	}

	return false;
}

static bool intel_pt_exclude_kernel(struct intel_pt *pt)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->attr, NULL) &&
		    !evsel->attr.exclude_kernel)
			return false;
	}
	return true;
}

static bool intel_pt_return_compression(struct intel_pt *pt)
{
	struct perf_evsel *evsel;
	u64 config;

	if (!pt->noretcomp_bit)
		return true;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->attr, &config) &&
		    (config & pt->noretcomp_bit))
			return false;
	}
	return true;
}

static unsigned int intel_pt_mtc_period(struct intel_pt *pt)
{
	struct perf_evsel *evsel;
	unsigned int shift;
	u64 config;

	if (!pt->mtc_freq_bits)
		return 0;

	for (shift = 0, config = pt->mtc_freq_bits; !(config & 1); shift++)
		config >>= 1;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->attr, &config))
			return (config & pt->mtc_freq_bits) >> shift;
	}
	return 0;
}

static bool intel_pt_timeless_decoding(struct intel_pt *pt)
{
	struct perf_evsel *evsel;
	bool timeless_decoding = true;
	u64 config;

	if (!pt->tsc_bit || !pt->cap_user_time_zero)
		return true;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (!(evsel->attr.sample_type & PERF_SAMPLE_TIME))
			return true;
		if (intel_pt_get_config(pt, &evsel->attr, &config)) {
			if (config & pt->tsc_bit)
				timeless_decoding = false;
			else
				return true;
		}
	}
	return timeless_decoding;
}

static bool intel_pt_tracing_kernel(struct intel_pt *pt)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->attr, NULL) &&
		    !evsel->attr.exclude_kernel)
			return true;
	}
	return false;
}

static bool intel_pt_have_tsc(struct intel_pt *pt)
{
	struct perf_evsel *evsel;
	bool have_tsc = false;
	u64 config;

	if (!pt->tsc_bit)
		return false;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->attr, &config)) {
			if (config & pt->tsc_bit)
				have_tsc = true;
			else
				return false;
		}
	}
	return have_tsc;
}

static u64 intel_pt_ns_to_ticks(const struct intel_pt *pt, u64 ns)
{
	u64 quot, rem;

	quot = ns / pt->tc.time_mult;
	rem  = ns % pt->tc.time_mult;
	return (quot << pt->tc.time_shift) + (rem << pt->tc.time_shift) /
		pt->tc.time_mult;
}

static struct intel_pt_queue *intel_pt_alloc_queue(struct intel_pt *pt,
						   unsigned int queue_nr)
{
	struct intel_pt_params params = { .get_trace = 0, };
	struct intel_pt_queue *ptq;

	ptq = zalloc(sizeof(struct intel_pt_queue));
	if (!ptq)
		return NULL;

	if (pt->synth_opts.callchain) {
		size_t sz = sizeof(struct ip_callchain);

		sz += pt->synth_opts.callchain_sz * sizeof(u64);
		ptq->chain = zalloc(sz);
		if (!ptq->chain)
			goto out_free;
	}

	if (pt->synth_opts.last_branch) {
		size_t sz = sizeof(struct branch_stack);

		sz += pt->synth_opts.last_branch_sz *
		      sizeof(struct branch_entry);
		ptq->last_branch = zalloc(sz);
		if (!ptq->last_branch)
			goto out_free;
		ptq->last_branch_rb = zalloc(sz);
		if (!ptq->last_branch_rb)
			goto out_free;
	}

	ptq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!ptq->event_buf)
		goto out_free;

	ptq->pt = pt;
	ptq->queue_nr = queue_nr;
	ptq->exclude_kernel = intel_pt_exclude_kernel(pt);
	ptq->pid = -1;
	ptq->tid = -1;
	ptq->cpu = -1;
	ptq->next_tid = -1;

	params.get_trace = intel_pt_get_trace;
	params.walk_insn = intel_pt_walk_next_insn;
	params.data = ptq;
	params.return_compression = intel_pt_return_compression(pt);
	params.max_non_turbo_ratio = pt->max_non_turbo_ratio;
	params.mtc_period = intel_pt_mtc_period(pt);
	params.tsc_ctc_ratio_n = pt->tsc_ctc_ratio_n;
	params.tsc_ctc_ratio_d = pt->tsc_ctc_ratio_d;

	if (pt->filts.cnt > 0)
		params.pgd_ip = intel_pt_pgd_ip;

	if (pt->synth_opts.instructions) {
		if (pt->synth_opts.period) {
			switch (pt->synth_opts.period_type) {
			case PERF_ITRACE_PERIOD_INSTRUCTIONS:
				params.period_type =
						INTEL_PT_PERIOD_INSTRUCTIONS;
				params.period = pt->synth_opts.period;
				break;
			case PERF_ITRACE_PERIOD_TICKS:
				params.period_type = INTEL_PT_PERIOD_TICKS;
				params.period = pt->synth_opts.period;
				break;
			case PERF_ITRACE_PERIOD_NANOSECS:
				params.period_type = INTEL_PT_PERIOD_TICKS;
				params.period = intel_pt_ns_to_ticks(pt,
							pt->synth_opts.period);
				break;
			default:
				break;
			}
		}

		if (!params.period) {
			params.period_type = INTEL_PT_PERIOD_INSTRUCTIONS;
			params.period = 1;
		}
	}

	ptq->decoder = intel_pt_decoder_new(&params);
	if (!ptq->decoder)
		goto out_free;

	return ptq;

out_free:
	zfree(&ptq->event_buf);
	zfree(&ptq->last_branch);
	zfree(&ptq->last_branch_rb);
	zfree(&ptq->chain);
	free(ptq);
	return NULL;
}

static void intel_pt_free_queue(void *priv)
{
	struct intel_pt_queue *ptq = priv;

	if (!ptq)
		return;
	thread__zput(ptq->thread);
	intel_pt_decoder_free(ptq->decoder);
	zfree(&ptq->event_buf);
	zfree(&ptq->last_branch);
	zfree(&ptq->last_branch_rb);
	zfree(&ptq->chain);
	free(ptq);
}

static void intel_pt_set_pid_tid_cpu(struct intel_pt *pt,
				     struct auxtrace_queue *queue)
{
	struct intel_pt_queue *ptq = queue->priv;

	if (queue->tid == -1 || pt->have_sched_switch) {
		ptq->tid = machine__get_current_tid(pt->machine, ptq->cpu);
		thread__zput(ptq->thread);
	}

	if (!ptq->thread && ptq->tid != -1)
		ptq->thread = machine__find_thread(pt->machine, -1, ptq->tid);

	if (ptq->thread) {
		ptq->pid = ptq->thread->pid_;
		if (queue->cpu == -1)
			ptq->cpu = ptq->thread->cpu;
	}
}

static void intel_pt_sample_flags(struct intel_pt_queue *ptq)
{
	if (ptq->state->flags & INTEL_PT_ABORT_TX) {
		ptq->flags = PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TX_ABORT;
	} else if (ptq->state->flags & INTEL_PT_ASYNC) {
		if (ptq->state->to_ip)
			ptq->flags = PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL |
				     PERF_IP_FLAG_ASYNC |
				     PERF_IP_FLAG_INTERRUPT;
		else
			ptq->flags = PERF_IP_FLAG_BRANCH |
				     PERF_IP_FLAG_TRACE_END;
		ptq->insn_len = 0;
	} else {
		if (ptq->state->from_ip)
			ptq->flags = intel_pt_insn_type(ptq->state->insn_op);
		else
			ptq->flags = PERF_IP_FLAG_BRANCH |
				     PERF_IP_FLAG_TRACE_BEGIN;
		if (ptq->state->flags & INTEL_PT_IN_TX)
			ptq->flags |= PERF_IP_FLAG_IN_TX;
		ptq->insn_len = ptq->state->insn_len;
	}
}

static int intel_pt_setup_queue(struct intel_pt *pt,
				struct auxtrace_queue *queue,
				unsigned int queue_nr)
{
	struct intel_pt_queue *ptq = queue->priv;

	if (list_empty(&queue->head))
		return 0;

	if (!ptq) {
		ptq = intel_pt_alloc_queue(pt, queue_nr);
		if (!ptq)
			return -ENOMEM;
		queue->priv = ptq;

		if (queue->cpu != -1)
			ptq->cpu = queue->cpu;
		ptq->tid = queue->tid;

		if (pt->sampling_mode) {
			if (pt->timeless_decoding)
				ptq->step_through_buffers = true;
			if (pt->timeless_decoding || !pt->have_sched_switch)
				ptq->use_buffer_pid_tid = true;
		}
	}

	if (!ptq->on_heap &&
	    (!pt->sync_switch ||
	     ptq->switch_state != INTEL_PT_SS_EXPECTING_SWITCH_EVENT)) {
		const struct intel_pt_state *state;
		int ret;

		if (pt->timeless_decoding)
			return 0;

		intel_pt_log("queue %u getting timestamp\n", queue_nr);
		intel_pt_log("queue %u decoding cpu %d pid %d tid %d\n",
			     queue_nr, ptq->cpu, ptq->pid, ptq->tid);
		while (1) {
			state = intel_pt_decode(ptq->decoder);
			if (state->err) {
				if (state->err == INTEL_PT_ERR_NODATA) {
					intel_pt_log("queue %u has no timestamp\n",
						     queue_nr);
					return 0;
				}
				continue;
			}
			if (state->timestamp)
				break;
		}

		ptq->timestamp = state->timestamp;
		intel_pt_log("queue %u timestamp 0x%" PRIx64 "\n",
			     queue_nr, ptq->timestamp);
		ptq->state = state;
		ptq->have_sample = true;
		intel_pt_sample_flags(ptq);
		ret = auxtrace_heap__add(&pt->heap, queue_nr, ptq->timestamp);
		if (ret)
			return ret;
		ptq->on_heap = true;
	}

	return 0;
}

static int intel_pt_setup_queues(struct intel_pt *pt)
{
	unsigned int i;
	int ret;

	for (i = 0; i < pt->queues.nr_queues; i++) {
		ret = intel_pt_setup_queue(pt, &pt->queues.queue_array[i], i);
		if (ret)
			return ret;
	}
	return 0;
}

static inline void intel_pt_copy_last_branch_rb(struct intel_pt_queue *ptq)
{
	struct branch_stack *bs_src = ptq->last_branch_rb;
	struct branch_stack *bs_dst = ptq->last_branch;
	size_t nr = 0;

	bs_dst->nr = bs_src->nr;

	if (!bs_src->nr)
		return;

	nr = ptq->pt->synth_opts.last_branch_sz - ptq->last_branch_pos;
	memcpy(&bs_dst->entries[0],
	       &bs_src->entries[ptq->last_branch_pos],
	       sizeof(struct branch_entry) * nr);

	if (bs_src->nr >= ptq->pt->synth_opts.last_branch_sz) {
		memcpy(&bs_dst->entries[nr],
		       &bs_src->entries[0],
		       sizeof(struct branch_entry) * ptq->last_branch_pos);
	}
}

static inline void intel_pt_reset_last_branch_rb(struct intel_pt_queue *ptq)
{
	ptq->last_branch_pos = 0;
	ptq->last_branch_rb->nr = 0;
}

static void intel_pt_update_last_branch_rb(struct intel_pt_queue *ptq)
{
	const struct intel_pt_state *state = ptq->state;
	struct branch_stack *bs = ptq->last_branch_rb;
	struct branch_entry *be;

	if (!ptq->last_branch_pos)
		ptq->last_branch_pos = ptq->pt->synth_opts.last_branch_sz;

	ptq->last_branch_pos -= 1;

	be              = &bs->entries[ptq->last_branch_pos];
	be->from        = state->from_ip;
	be->to          = state->to_ip;
	be->flags.abort = !!(state->flags & INTEL_PT_ABORT_TX);
	be->flags.in_tx = !!(state->flags & INTEL_PT_IN_TX);
	/* No support for mispredict */
	be->flags.mispred = ptq->pt->mispred_all;

	if (bs->nr < ptq->pt->synth_opts.last_branch_sz)
		bs->nr += 1;
}

static int intel_pt_inject_event(union perf_event *event,
				 struct perf_sample *sample, u64 type,
				 bool swapped)
{
	event->header.size = perf_event__sample_event_size(sample, type, 0);
	return perf_event__synthesize_sample(event, type, 0, sample, swapped);
}

static int intel_pt_synth_branch_sample(struct intel_pt_queue *ptq)
{
	int ret;
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct dummy_branch_stack {
		u64			nr;
		struct branch_entry	entries;
	} dummy_bs;

	if (pt->branches_filter && !(pt->branches_filter & ptq->flags))
		return 0;

	if (pt->synth_opts.initial_skip &&
	    pt->num_events++ < pt->synth_opts.initial_skip)
		return 0;

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = PERF_RECORD_MISC_USER;
	event->sample.header.size = sizeof(struct perf_event_header);

	if (!pt->timeless_decoding)
		sample.time = tsc_to_perf_time(ptq->timestamp, &pt->tc);

	sample.cpumode = PERF_RECORD_MISC_USER;
	sample.ip = ptq->state->from_ip;
	sample.pid = ptq->pid;
	sample.tid = ptq->tid;
	sample.addr = ptq->state->to_ip;
	sample.id = ptq->pt->branches_id;
	sample.stream_id = ptq->pt->branches_id;
	sample.period = 1;
	sample.cpu = ptq->cpu;
	sample.flags = ptq->flags;
	sample.insn_len = ptq->insn_len;

	/*
	 * perf report cannot handle events without a branch stack when using
	 * SORT_MODE__BRANCH so make a dummy one.
	 */
	if (pt->synth_opts.last_branch && sort__mode == SORT_MODE__BRANCH) {
		dummy_bs = (struct dummy_branch_stack){
			.nr = 1,
			.entries = {
				.from = sample.ip,
				.to = sample.addr,
			},
		};
		sample.branch_stack = (struct branch_stack *)&dummy_bs;
	}

	if (pt->synth_opts.inject) {
		ret = intel_pt_inject_event(event, &sample,
					    pt->branches_sample_type,
					    pt->synth_needs_swap);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(pt->session, event, &sample);
	if (ret)
		pr_err("Intel Processor Trace: failed to deliver branch event, error %d\n",
		       ret);

	return ret;
}

static int intel_pt_synth_instruction_sample(struct intel_pt_queue *ptq)
{
	int ret;
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };

	if (pt->synth_opts.initial_skip &&
	    pt->num_events++ < pt->synth_opts.initial_skip)
		return 0;

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = PERF_RECORD_MISC_USER;
	event->sample.header.size = sizeof(struct perf_event_header);

	if (!pt->timeless_decoding)
		sample.time = tsc_to_perf_time(ptq->timestamp, &pt->tc);

	sample.cpumode = PERF_RECORD_MISC_USER;
	sample.ip = ptq->state->from_ip;
	sample.pid = ptq->pid;
	sample.tid = ptq->tid;
	sample.addr = ptq->state->to_ip;
	sample.id = ptq->pt->instructions_id;
	sample.stream_id = ptq->pt->instructions_id;
	sample.period = ptq->state->tot_insn_cnt - ptq->last_insn_cnt;
	sample.cpu = ptq->cpu;
	sample.flags = ptq->flags;
	sample.insn_len = ptq->insn_len;

	ptq->last_insn_cnt = ptq->state->tot_insn_cnt;

	if (pt->synth_opts.callchain) {
		thread_stack__sample(ptq->thread, ptq->chain,
				     pt->synth_opts.callchain_sz, sample.ip);
		sample.callchain = ptq->chain;
	}

	if (pt->synth_opts.last_branch) {
		intel_pt_copy_last_branch_rb(ptq);
		sample.branch_stack = ptq->last_branch;
	}

	if (pt->synth_opts.inject) {
		ret = intel_pt_inject_event(event, &sample,
					    pt->instructions_sample_type,
					    pt->synth_needs_swap);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(pt->session, event, &sample);
	if (ret)
		pr_err("Intel Processor Trace: failed to deliver instruction event, error %d\n",
		       ret);

	if (pt->synth_opts.last_branch)
		intel_pt_reset_last_branch_rb(ptq);

	return ret;
}

static int intel_pt_synth_transaction_sample(struct intel_pt_queue *ptq)
{
	int ret;
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };

	if (pt->synth_opts.initial_skip &&
	    pt->num_events++ < pt->synth_opts.initial_skip)
		return 0;

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = PERF_RECORD_MISC_USER;
	event->sample.header.size = sizeof(struct perf_event_header);

	if (!pt->timeless_decoding)
		sample.time = tsc_to_perf_time(ptq->timestamp, &pt->tc);

	sample.cpumode = PERF_RECORD_MISC_USER;
	sample.ip = ptq->state->from_ip;
	sample.pid = ptq->pid;
	sample.tid = ptq->tid;
	sample.addr = ptq->state->to_ip;
	sample.id = ptq->pt->transactions_id;
	sample.stream_id = ptq->pt->transactions_id;
	sample.period = 1;
	sample.cpu = ptq->cpu;
	sample.flags = ptq->flags;
	sample.insn_len = ptq->insn_len;

	if (pt->synth_opts.callchain) {
		thread_stack__sample(ptq->thread, ptq->chain,
				     pt->synth_opts.callchain_sz, sample.ip);
		sample.callchain = ptq->chain;
	}

	if (pt->synth_opts.last_branch) {
		intel_pt_copy_last_branch_rb(ptq);
		sample.branch_stack = ptq->last_branch;
	}

	if (pt->synth_opts.inject) {
		ret = intel_pt_inject_event(event, &sample,
					    pt->transactions_sample_type,
					    pt->synth_needs_swap);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(pt->session, event, &sample);
	if (ret)
		pr_err("Intel Processor Trace: failed to deliver transaction event, error %d\n",
		       ret);

	if (pt->synth_opts.last_branch)
		intel_pt_reset_last_branch_rb(ptq);

	return ret;
}

static int intel_pt_synth_error(struct intel_pt *pt, int code, int cpu,
				pid_t pid, pid_t tid, u64 ip)
{
	union perf_event event;
	char msg[MAX_AUXTRACE_ERROR_MSG];
	int err;

	intel_pt__strerror(code, msg, MAX_AUXTRACE_ERROR_MSG);

	auxtrace_synth_error(&event.auxtrace_error, PERF_AUXTRACE_ERROR_ITRACE,
			     code, cpu, pid, tid, ip, msg);

	err = perf_session__deliver_synth_event(pt->session, &event, NULL);
	if (err)
		pr_err("Intel Processor Trace: failed to deliver error event, error %d\n",
		       err);

	return err;
}

static int intel_pt_next_tid(struct intel_pt *pt, struct intel_pt_queue *ptq)
{
	struct auxtrace_queue *queue;
	pid_t tid = ptq->next_tid;
	int err;

	if (tid == -1)
		return 0;

	intel_pt_log("switch: cpu %d tid %d\n", ptq->cpu, tid);

	err = machine__set_current_tid(pt->machine, ptq->cpu, -1, tid);

	queue = &pt->queues.queue_array[ptq->queue_nr];
	intel_pt_set_pid_tid_cpu(pt, queue);

	ptq->next_tid = -1;

	return err;
}

static inline bool intel_pt_is_switch_ip(struct intel_pt_queue *ptq, u64 ip)
{
	struct intel_pt *pt = ptq->pt;

	return ip == pt->switch_ip &&
	       (ptq->flags & PERF_IP_FLAG_BRANCH) &&
	       !(ptq->flags & (PERF_IP_FLAG_CONDITIONAL | PERF_IP_FLAG_ASYNC |
			       PERF_IP_FLAG_INTERRUPT | PERF_IP_FLAG_TX_ABORT));
}

static int intel_pt_sample(struct intel_pt_queue *ptq)
{
	const struct intel_pt_state *state = ptq->state;
	struct intel_pt *pt = ptq->pt;
	int err;

	if (!ptq->have_sample)
		return 0;

	ptq->have_sample = false;

	if (pt->sample_instructions &&
	    (state->type & INTEL_PT_INSTRUCTION) &&
	    (!pt->synth_opts.initial_skip ||
	     pt->num_events++ >= pt->synth_opts.initial_skip)) {
		err = intel_pt_synth_instruction_sample(ptq);
		if (err)
			return err;
	}

	if (pt->sample_transactions &&
	    (state->type & INTEL_PT_TRANSACTION) &&
	    (!pt->synth_opts.initial_skip ||
	     pt->num_events++ >= pt->synth_opts.initial_skip)) {
		err = intel_pt_synth_transaction_sample(ptq);
		if (err)
			return err;
	}

	if (!(state->type & INTEL_PT_BRANCH))
		return 0;

	if (pt->synth_opts.callchain || pt->synth_opts.thread_stack)
		thread_stack__event(ptq->thread, ptq->flags, state->from_ip,
				    state->to_ip, ptq->insn_len,
				    state->trace_nr);
	else
		thread_stack__set_trace_nr(ptq->thread, state->trace_nr);

	if (pt->sample_branches) {
		err = intel_pt_synth_branch_sample(ptq);
		if (err)
			return err;
	}

	if (pt->synth_opts.last_branch)
		intel_pt_update_last_branch_rb(ptq);

	if (!pt->sync_switch)
		return 0;

	if (intel_pt_is_switch_ip(ptq, state->to_ip)) {
		switch (ptq->switch_state) {
		case INTEL_PT_SS_UNKNOWN:
		case INTEL_PT_SS_EXPECTING_SWITCH_IP:
			err = intel_pt_next_tid(pt, ptq);
			if (err)
				return err;
			ptq->switch_state = INTEL_PT_SS_TRACING;
			break;
		default:
			ptq->switch_state = INTEL_PT_SS_EXPECTING_SWITCH_EVENT;
			return 1;
		}
	} else if (!state->to_ip) {
		ptq->switch_state = INTEL_PT_SS_NOT_TRACING;
	} else if (ptq->switch_state == INTEL_PT_SS_NOT_TRACING) {
		ptq->switch_state = INTEL_PT_SS_UNKNOWN;
	} else if (ptq->switch_state == INTEL_PT_SS_UNKNOWN &&
		   state->to_ip == pt->ptss_ip &&
		   (ptq->flags & PERF_IP_FLAG_CALL)) {
		ptq->switch_state = INTEL_PT_SS_TRACING;
	}

	return 0;
}

static u64 intel_pt_switch_ip(struct intel_pt *pt, u64 *ptss_ip)
{
	struct machine *machine = pt->machine;
	struct map *map;
	struct symbol *sym, *start;
	u64 ip, switch_ip = 0;
	const char *ptss;

	if (ptss_ip)
		*ptss_ip = 0;

	map = machine__kernel_map(machine);
	if (!map)
		return 0;

	if (map__load(map))
		return 0;

	start = dso__first_symbol(map->dso, MAP__FUNCTION);

	for (sym = start; sym; sym = dso__next_symbol(sym)) {
		if (sym->binding == STB_GLOBAL &&
		    !strcmp(sym->name, "__switch_to")) {
			ip = map->unmap_ip(map, sym->start);
			if (ip >= map->start && ip < map->end) {
				switch_ip = ip;
				break;
			}
		}
	}

	if (!switch_ip || !ptss_ip)
		return 0;

	if (pt->have_sched_switch == 1)
		ptss = "perf_trace_sched_switch";
	else
		ptss = "__perf_event_task_sched_out";

	for (sym = start; sym; sym = dso__next_symbol(sym)) {
		if (!strcmp(sym->name, ptss)) {
			ip = map->unmap_ip(map, sym->start);
			if (ip >= map->start && ip < map->end) {
				*ptss_ip = ip;
				break;
			}
		}
	}

	return switch_ip;
}

static int intel_pt_run_decoder(struct intel_pt_queue *ptq, u64 *timestamp)
{
	const struct intel_pt_state *state = ptq->state;
	struct intel_pt *pt = ptq->pt;
	int err;

	if (!pt->kernel_start) {
		pt->kernel_start = machine__kernel_start(pt->machine);
		if (pt->per_cpu_mmaps &&
		    (pt->have_sched_switch == 1 || pt->have_sched_switch == 3) &&
		    !pt->timeless_decoding && intel_pt_tracing_kernel(pt) &&
		    !pt->sampling_mode) {
			pt->switch_ip = intel_pt_switch_ip(pt, &pt->ptss_ip);
			if (pt->switch_ip) {
				intel_pt_log("switch_ip: %"PRIx64" ptss_ip: %"PRIx64"\n",
					     pt->switch_ip, pt->ptss_ip);
				pt->sync_switch = true;
			}
		}
	}

	intel_pt_log("queue %u decoding cpu %d pid %d tid %d\n",
		     ptq->queue_nr, ptq->cpu, ptq->pid, ptq->tid);
	while (1) {
		err = intel_pt_sample(ptq);
		if (err)
			return err;

		state = intel_pt_decode(ptq->decoder);
		if (state->err) {
			if (state->err == INTEL_PT_ERR_NODATA)
				return 1;
			if (pt->sync_switch &&
			    state->from_ip >= pt->kernel_start) {
				pt->sync_switch = false;
				intel_pt_next_tid(pt, ptq);
			}
			if (pt->synth_opts.errors) {
				err = intel_pt_synth_error(pt, state->err,
							   ptq->cpu, ptq->pid,
							   ptq->tid,
							   state->from_ip);
				if (err)
					return err;
			}
			continue;
		}

		ptq->state = state;
		ptq->have_sample = true;
		intel_pt_sample_flags(ptq);

		/* Use estimated TSC upon return to user space */
		if (pt->est_tsc &&
		    (state->from_ip >= pt->kernel_start || !state->from_ip) &&
		    state->to_ip && state->to_ip < pt->kernel_start) {
			intel_pt_log("TSC %"PRIx64" est. TSC %"PRIx64"\n",
				     state->timestamp, state->est_timestamp);
			ptq->timestamp = state->est_timestamp;
		/* Use estimated TSC in unknown switch state */
		} else if (pt->sync_switch &&
			   ptq->switch_state == INTEL_PT_SS_UNKNOWN &&
			   intel_pt_is_switch_ip(ptq, state->to_ip) &&
			   ptq->next_tid == -1) {
			intel_pt_log("TSC %"PRIx64" est. TSC %"PRIx64"\n",
				     state->timestamp, state->est_timestamp);
			ptq->timestamp = state->est_timestamp;
		} else if (state->timestamp > ptq->timestamp) {
			ptq->timestamp = state->timestamp;
		}

		if (!pt->timeless_decoding && ptq->timestamp >= *timestamp) {
			*timestamp = ptq->timestamp;
			return 0;
		}
	}
	return 0;
}

static inline int intel_pt_update_queues(struct intel_pt *pt)
{
	if (pt->queues.new_data) {
		pt->queues.new_data = false;
		return intel_pt_setup_queues(pt);
	}
	return 0;
}

static int intel_pt_process_queues(struct intel_pt *pt, u64 timestamp)
{
	unsigned int queue_nr;
	u64 ts;
	int ret;

	while (1) {
		struct auxtrace_queue *queue;
		struct intel_pt_queue *ptq;

		if (!pt->heap.heap_cnt)
			return 0;

		if (pt->heap.heap_array[0].ordinal >= timestamp)
			return 0;

		queue_nr = pt->heap.heap_array[0].queue_nr;
		queue = &pt->queues.queue_array[queue_nr];
		ptq = queue->priv;

		intel_pt_log("queue %u processing 0x%" PRIx64 " to 0x%" PRIx64 "\n",
			     queue_nr, pt->heap.heap_array[0].ordinal,
			     timestamp);

		auxtrace_heap__pop(&pt->heap);

		if (pt->heap.heap_cnt) {
			ts = pt->heap.heap_array[0].ordinal + 1;
			if (ts > timestamp)
				ts = timestamp;
		} else {
			ts = timestamp;
		}

		intel_pt_set_pid_tid_cpu(pt, queue);

		ret = intel_pt_run_decoder(ptq, &ts);

		if (ret < 0) {
			auxtrace_heap__add(&pt->heap, queue_nr, ts);
			return ret;
		}

		if (!ret) {
			ret = auxtrace_heap__add(&pt->heap, queue_nr, ts);
			if (ret < 0)
				return ret;
		} else {
			ptq->on_heap = false;
		}
	}

	return 0;
}

static int intel_pt_process_timeless_queues(struct intel_pt *pt, pid_t tid,
					    u64 time_)
{
	struct auxtrace_queues *queues = &pt->queues;
	unsigned int i;
	u64 ts = 0;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &pt->queues.queue_array[i];
		struct intel_pt_queue *ptq = queue->priv;

		if (ptq && (tid == -1 || ptq->tid == tid)) {
			ptq->time = time_;
			intel_pt_set_pid_tid_cpu(pt, queue);
			intel_pt_run_decoder(ptq, &ts);
		}
	}
	return 0;
}

static int intel_pt_lost(struct intel_pt *pt, struct perf_sample *sample)
{
	return intel_pt_synth_error(pt, INTEL_PT_ERR_LOST, sample->cpu,
				    sample->pid, sample->tid, 0);
}

static struct intel_pt_queue *intel_pt_cpu_to_ptq(struct intel_pt *pt, int cpu)
{
	unsigned i, j;

	if (cpu < 0 || !pt->queues.nr_queues)
		return NULL;

	if ((unsigned)cpu >= pt->queues.nr_queues)
		i = pt->queues.nr_queues - 1;
	else
		i = cpu;

	if (pt->queues.queue_array[i].cpu == cpu)
		return pt->queues.queue_array[i].priv;

	for (j = 0; i > 0; j++) {
		if (pt->queues.queue_array[--i].cpu == cpu)
			return pt->queues.queue_array[i].priv;
	}

	for (; j < pt->queues.nr_queues; j++) {
		if (pt->queues.queue_array[j].cpu == cpu)
			return pt->queues.queue_array[j].priv;
	}

	return NULL;
}

static int intel_pt_sync_switch(struct intel_pt *pt, int cpu, pid_t tid,
				u64 timestamp)
{
	struct intel_pt_queue *ptq;
	int err;

	if (!pt->sync_switch)
		return 1;

	ptq = intel_pt_cpu_to_ptq(pt, cpu);
	if (!ptq)
		return 1;

	switch (ptq->switch_state) {
	case INTEL_PT_SS_NOT_TRACING:
		ptq->next_tid = -1;
		break;
	case INTEL_PT_SS_UNKNOWN:
	case INTEL_PT_SS_TRACING:
		ptq->next_tid = tid;
		ptq->switch_state = INTEL_PT_SS_EXPECTING_SWITCH_IP;
		return 0;
	case INTEL_PT_SS_EXPECTING_SWITCH_EVENT:
		if (!ptq->on_heap) {
			ptq->timestamp = perf_time_to_tsc(timestamp,
							  &pt->tc);
			err = auxtrace_heap__add(&pt->heap, ptq->queue_nr,
						 ptq->timestamp);
			if (err)
				return err;
			ptq->on_heap = true;
		}
		ptq->switch_state = INTEL_PT_SS_TRACING;
		break;
	case INTEL_PT_SS_EXPECTING_SWITCH_IP:
		ptq->next_tid = tid;
		intel_pt_log("ERROR: cpu %d expecting switch ip\n", cpu);
		break;
	default:
		break;
	}

	return 1;
}

static int intel_pt_process_switch(struct intel_pt *pt,
				   struct perf_sample *sample)
{
	struct perf_evsel *evsel;
	pid_t tid;
	int cpu, ret;

	evsel = perf_evlist__id2evsel(pt->session->evlist, sample->id);
	if (evsel != pt->switch_evsel)
		return 0;

	tid = perf_evsel__intval(evsel, sample, "next_pid");
	cpu = sample->cpu;

	intel_pt_log("sched_switch: cpu %d tid %d time %"PRIu64" tsc %#"PRIx64"\n",
		     cpu, tid, sample->time, perf_time_to_tsc(sample->time,
		     &pt->tc));

	ret = intel_pt_sync_switch(pt, cpu, tid, sample->time);
	if (ret <= 0)
		return ret;

	return machine__set_current_tid(pt->machine, cpu, -1, tid);
}

static int intel_pt_context_switch(struct intel_pt *pt, union perf_event *event,
				   struct perf_sample *sample)
{
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	pid_t pid, tid;
	int cpu, ret;

	cpu = sample->cpu;

	if (pt->have_sched_switch == 3) {
		if (!out)
			return 0;
		if (event->header.type != PERF_RECORD_SWITCH_CPU_WIDE) {
			pr_err("Expecting CPU-wide context switch event\n");
			return -EINVAL;
		}
		pid = event->context_switch.next_prev_pid;
		tid = event->context_switch.next_prev_tid;
	} else {
		if (out)
			return 0;
		pid = sample->pid;
		tid = sample->tid;
	}

	if (tid == -1) {
		pr_err("context_switch event has no tid\n");
		return -EINVAL;
	}

	intel_pt_log("context_switch: cpu %d pid %d tid %d time %"PRIu64" tsc %#"PRIx64"\n",
		     cpu, pid, tid, sample->time, perf_time_to_tsc(sample->time,
		     &pt->tc));

	ret = intel_pt_sync_switch(pt, cpu, tid, sample->time);
	if (ret <= 0)
		return ret;

	return machine__set_current_tid(pt->machine, cpu, pid, tid);
}

static int intel_pt_process_itrace_start(struct intel_pt *pt,
					 union perf_event *event,
					 struct perf_sample *sample)
{
	if (!pt->per_cpu_mmaps)
		return 0;

	intel_pt_log("itrace_start: cpu %d pid %d tid %d time %"PRIu64" tsc %#"PRIx64"\n",
		     sample->cpu, event->itrace_start.pid,
		     event->itrace_start.tid, sample->time,
		     perf_time_to_tsc(sample->time, &pt->tc));

	return machine__set_current_tid(pt->machine, sample->cpu,
					event->itrace_start.pid,
					event->itrace_start.tid);
}

static int intel_pt_process_event(struct perf_session *session,
				  union perf_event *event,
				  struct perf_sample *sample,
				  struct perf_tool *tool)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	u64 timestamp;
	int err = 0;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("Intel Processor Trace requires ordered events\n");
		return -EINVAL;
	}

	if (sample->time && sample->time != (u64)-1)
		timestamp = perf_time_to_tsc(sample->time, &pt->tc);
	else
		timestamp = 0;

	if (timestamp || pt->timeless_decoding) {
		err = intel_pt_update_queues(pt);
		if (err)
			return err;
	}

	if (pt->timeless_decoding) {
		if (event->header.type == PERF_RECORD_EXIT) {
			err = intel_pt_process_timeless_queues(pt,
							       event->fork.tid,
							       sample->time);
		}
	} else if (timestamp) {
		err = intel_pt_process_queues(pt, timestamp);
	}
	if (err)
		return err;

	if (event->header.type == PERF_RECORD_AUX &&
	    (event->aux.flags & PERF_AUX_FLAG_TRUNCATED) &&
	    pt->synth_opts.errors) {
		err = intel_pt_lost(pt, sample);
		if (err)
			return err;
	}

	if (pt->switch_evsel && event->header.type == PERF_RECORD_SAMPLE)
		err = intel_pt_process_switch(pt, sample);
	else if (event->header.type == PERF_RECORD_ITRACE_START)
		err = intel_pt_process_itrace_start(pt, event, sample);
	else if (event->header.type == PERF_RECORD_SWITCH ||
		 event->header.type == PERF_RECORD_SWITCH_CPU_WIDE)
		err = intel_pt_context_switch(pt, event, sample);

	intel_pt_log("event %s (%u): cpu %d time %"PRIu64" tsc %#"PRIx64"\n",
		     perf_event__name(event->header.type), event->header.type,
		     sample->cpu, sample->time, timestamp);

	return err;
}

static int intel_pt_flush(struct perf_session *session, struct perf_tool *tool)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	int ret;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events)
		return -EINVAL;

	ret = intel_pt_update_queues(pt);
	if (ret < 0)
		return ret;

	if (pt->timeless_decoding)
		return intel_pt_process_timeless_queues(pt, -1,
							MAX_TIMESTAMP - 1);

	return intel_pt_process_queues(pt, MAX_TIMESTAMP);
}

static void intel_pt_free_events(struct perf_session *session)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	struct auxtrace_queues *queues = &pt->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		intel_pt_free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}
	intel_pt_log_disable();
	auxtrace_queues__free(queues);
}

static void intel_pt_free(struct perf_session *session)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	auxtrace_heap__free(&pt->heap);
	intel_pt_free_events(session);
	session->auxtrace = NULL;
	thread__put(pt->unknown_thread);
	addr_filters__exit(&pt->filts);
	zfree(&pt->filter);
	free(pt);
}

static int intel_pt_process_auxtrace_event(struct perf_session *session,
					   union perf_event *event,
					   struct perf_tool *tool __maybe_unused)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	if (pt->sampling_mode)
		return 0;

	if (!pt->data_queued) {
		struct auxtrace_buffer *buffer;
		off_t data_offset;
		int fd = perf_data_file__fd(session->file);
		int err;

		if (perf_data_file__is_pipe(session->file)) {
			data_offset = 0;
		} else {
			data_offset = lseek(fd, 0, SEEK_CUR);
			if (data_offset == -1)
				return -errno;
		}

		err = auxtrace_queues__add_event(&pt->queues, session, event,
						 data_offset, &buffer);
		if (err)
			return err;

		/* Dump here now we have copied a piped trace out of the pipe */
		if (dump_trace) {
			if (auxtrace_buffer__get_data(buffer, fd)) {
				intel_pt_dump_event(pt, buffer->data,
						    buffer->size);
				auxtrace_buffer__put_data(buffer);
			}
		}
	}

	return 0;
}

struct intel_pt_synth {
	struct perf_tool dummy_tool;
	struct perf_session *session;
};

static int intel_pt_event_synth(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample __maybe_unused,
				struct machine *machine __maybe_unused)
{
	struct intel_pt_synth *intel_pt_synth =
			container_of(tool, struct intel_pt_synth, dummy_tool);

	return perf_session__deliver_synth_event(intel_pt_synth->session, event,
						 NULL);
}

static int intel_pt_synth_event(struct perf_session *session,
				struct perf_event_attr *attr, u64 id)
{
	struct intel_pt_synth intel_pt_synth;

	memset(&intel_pt_synth, 0, sizeof(struct intel_pt_synth));
	intel_pt_synth.session = session;

	return perf_event__synthesize_attr(&intel_pt_synth.dummy_tool, attr, 1,
					   &id, intel_pt_event_synth);
}

static int intel_pt_synth_events(struct intel_pt *pt,
				 struct perf_session *session)
{
	struct perf_evlist *evlist = session->evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type == pt->pmu_type && evsel->ids) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("There are no selected events with Intel Processor Trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
			    PERF_SAMPLE_PERIOD;
	if (pt->timeless_decoding)
		attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	else
		attr.sample_type |= PERF_SAMPLE_TIME;
	if (!pt->per_cpu_mmaps)
		attr.sample_type &= ~(u64)PERF_SAMPLE_CPU;
	attr.exclude_user = evsel->attr.exclude_user;
	attr.exclude_kernel = evsel->attr.exclude_kernel;
	attr.exclude_hv = evsel->attr.exclude_hv;
	attr.exclude_host = evsel->attr.exclude_host;
	attr.exclude_guest = evsel->attr.exclude_guest;
	attr.sample_id_all = evsel->attr.sample_id_all;
	attr.read_format = evsel->attr.read_format;

	id = evsel->id[0] + 1000000000;
	if (!id)
		id = 1;

	if (pt->synth_opts.instructions) {
		attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		if (pt->synth_opts.period_type == PERF_ITRACE_PERIOD_NANOSECS)
			attr.sample_period =
				intel_pt_ns_to_ticks(pt, pt->synth_opts.period);
		else
			attr.sample_period = pt->synth_opts.period;
		pt->instructions_sample_period = attr.sample_period;
		if (pt->synth_opts.callchain)
			attr.sample_type |= PERF_SAMPLE_CALLCHAIN;
		if (pt->synth_opts.last_branch)
			attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;
		pr_debug("Synthesizing 'instructions' event with id %" PRIu64 " sample type %#" PRIx64 "\n",
			 id, (u64)attr.sample_type);
		err = intel_pt_synth_event(session, &attr, id);
		if (err) {
			pr_err("%s: failed to synthesize 'instructions' event type\n",
			       __func__);
			return err;
		}
		pt->sample_instructions = true;
		pt->instructions_sample_type = attr.sample_type;
		pt->instructions_id = id;
		id += 1;
	}

	if (pt->synth_opts.transactions) {
		attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		attr.sample_period = 1;
		if (pt->synth_opts.callchain)
			attr.sample_type |= PERF_SAMPLE_CALLCHAIN;
		if (pt->synth_opts.last_branch)
			attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;
		pr_debug("Synthesizing 'transactions' event with id %" PRIu64 " sample type %#" PRIx64 "\n",
			 id, (u64)attr.sample_type);
		err = intel_pt_synth_event(session, &attr, id);
		if (err) {
			pr_err("%s: failed to synthesize 'transactions' event type\n",
			       __func__);
			return err;
		}
		pt->sample_transactions = true;
		pt->transactions_id = id;
		id += 1;
		evlist__for_each_entry(evlist, evsel) {
			if (evsel->id && evsel->id[0] == pt->transactions_id) {
				if (evsel->name)
					zfree(&evsel->name);
				evsel->name = strdup("transactions");
				break;
			}
		}
	}

	if (pt->synth_opts.branches) {
		attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		attr.sample_period = 1;
		attr.sample_type |= PERF_SAMPLE_ADDR;
		attr.sample_type &= ~(u64)PERF_SAMPLE_CALLCHAIN;
		attr.sample_type &= ~(u64)PERF_SAMPLE_BRANCH_STACK;
		pr_debug("Synthesizing 'branches' event with id %" PRIu64 " sample type %#" PRIx64 "\n",
			 id, (u64)attr.sample_type);
		err = intel_pt_synth_event(session, &attr, id);
		if (err) {
			pr_err("%s: failed to synthesize 'branches' event type\n",
			       __func__);
			return err;
		}
		pt->sample_branches = true;
		pt->branches_sample_type = attr.sample_type;
		pt->branches_id = id;
	}

	pt->synth_needs_swap = evsel->needs_swap;

	return 0;
}

static struct perf_evsel *intel_pt_find_sched_switch(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry_reverse(evlist, evsel) {
		const char *name = perf_evsel__name(evsel);

		if (!strcmp(name, "sched:sched_switch"))
			return evsel;
	}

	return NULL;
}

static bool intel_pt_find_switch(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.context_switch)
			return true;
	}

	return false;
}

static int intel_pt_perf_config(const char *var, const char *value, void *data)
{
	struct intel_pt *pt = data;

	if (!strcmp(var, "intel-pt.mispred-all"))
		pt->mispred_all = perf_config_bool(var, value);

	return 0;
}

static const char * const intel_pt_info_fmts[] = {
	[INTEL_PT_PMU_TYPE]		= "  PMU Type            %"PRId64"\n",
	[INTEL_PT_TIME_SHIFT]		= "  Time Shift          %"PRIu64"\n",
	[INTEL_PT_TIME_MULT]		= "  Time Muliplier      %"PRIu64"\n",
	[INTEL_PT_TIME_ZERO]		= "  Time Zero           %"PRIu64"\n",
	[INTEL_PT_CAP_USER_TIME_ZERO]	= "  Cap Time Zero       %"PRId64"\n",
	[INTEL_PT_TSC_BIT]		= "  TSC bit             %#"PRIx64"\n",
	[INTEL_PT_NORETCOMP_BIT]	= "  NoRETComp bit       %#"PRIx64"\n",
	[INTEL_PT_HAVE_SCHED_SWITCH]	= "  Have sched_switch   %"PRId64"\n",
	[INTEL_PT_SNAPSHOT_MODE]	= "  Snapshot mode       %"PRId64"\n",
	[INTEL_PT_PER_CPU_MMAPS]	= "  Per-cpu maps        %"PRId64"\n",
	[INTEL_PT_MTC_BIT]		= "  MTC bit             %#"PRIx64"\n",
	[INTEL_PT_TSC_CTC_N]		= "  TSC:CTC numerator   %"PRIu64"\n",
	[INTEL_PT_TSC_CTC_D]		= "  TSC:CTC denominator %"PRIu64"\n",
	[INTEL_PT_CYC_BIT]		= "  CYC bit             %#"PRIx64"\n",
	[INTEL_PT_MAX_NONTURBO_RATIO]	= "  Max non-turbo ratio %"PRIu64"\n",
	[INTEL_PT_FILTER_STR_LEN]	= "  Filter string len.  %"PRIu64"\n",
};

static void intel_pt_print_info(u64 *arr, int start, int finish)
{
	int i;

	if (!dump_trace)
		return;

	for (i = start; i <= finish; i++)
		fprintf(stdout, intel_pt_info_fmts[i], arr[i]);
}

static void intel_pt_print_info_str(const char *name, const char *str)
{
	if (!dump_trace)
		return;

	fprintf(stdout, "  %-20s%s\n", name, str ? str : "");
}

static bool intel_pt_has(struct auxtrace_info_event *auxtrace_info, int pos)
{
	return auxtrace_info->header.size >=
		sizeof(struct auxtrace_info_event) + (sizeof(u64) * (pos + 1));
}

int intel_pt_process_auxtrace_info(union perf_event *event,
				   struct perf_session *session)
{
	struct auxtrace_info_event *auxtrace_info = &event->auxtrace_info;
	size_t min_sz = sizeof(u64) * INTEL_PT_PER_CPU_MMAPS;
	struct intel_pt *pt;
	void *info_end;
	u64 *info;
	int err;

	if (auxtrace_info->header.size < sizeof(struct auxtrace_info_event) +
					min_sz)
		return -EINVAL;

	pt = zalloc(sizeof(struct intel_pt));
	if (!pt)
		return -ENOMEM;

	addr_filters__init(&pt->filts);

	perf_config(intel_pt_perf_config, pt);

	err = auxtrace_queues__init(&pt->queues);
	if (err)
		goto err_free;

	intel_pt_log_set_name(INTEL_PT_PMU_NAME);

	pt->session = session;
	pt->machine = &session->machines.host; /* No kvm support */
	pt->auxtrace_type = auxtrace_info->type;
	pt->pmu_type = auxtrace_info->priv[INTEL_PT_PMU_TYPE];
	pt->tc.time_shift = auxtrace_info->priv[INTEL_PT_TIME_SHIFT];
	pt->tc.time_mult = auxtrace_info->priv[INTEL_PT_TIME_MULT];
	pt->tc.time_zero = auxtrace_info->priv[INTEL_PT_TIME_ZERO];
	pt->cap_user_time_zero = auxtrace_info->priv[INTEL_PT_CAP_USER_TIME_ZERO];
	pt->tsc_bit = auxtrace_info->priv[INTEL_PT_TSC_BIT];
	pt->noretcomp_bit = auxtrace_info->priv[INTEL_PT_NORETCOMP_BIT];
	pt->have_sched_switch = auxtrace_info->priv[INTEL_PT_HAVE_SCHED_SWITCH];
	pt->snapshot_mode = auxtrace_info->priv[INTEL_PT_SNAPSHOT_MODE];
	pt->per_cpu_mmaps = auxtrace_info->priv[INTEL_PT_PER_CPU_MMAPS];
	intel_pt_print_info(&auxtrace_info->priv[0], INTEL_PT_PMU_TYPE,
			    INTEL_PT_PER_CPU_MMAPS);

	if (intel_pt_has(auxtrace_info, INTEL_PT_CYC_BIT)) {
		pt->mtc_bit = auxtrace_info->priv[INTEL_PT_MTC_BIT];
		pt->mtc_freq_bits = auxtrace_info->priv[INTEL_PT_MTC_FREQ_BITS];
		pt->tsc_ctc_ratio_n = auxtrace_info->priv[INTEL_PT_TSC_CTC_N];
		pt->tsc_ctc_ratio_d = auxtrace_info->priv[INTEL_PT_TSC_CTC_D];
		pt->cyc_bit = auxtrace_info->priv[INTEL_PT_CYC_BIT];
		intel_pt_print_info(&auxtrace_info->priv[0], INTEL_PT_MTC_BIT,
				    INTEL_PT_CYC_BIT);
	}

	if (intel_pt_has(auxtrace_info, INTEL_PT_MAX_NONTURBO_RATIO)) {
		pt->max_non_turbo_ratio =
			auxtrace_info->priv[INTEL_PT_MAX_NONTURBO_RATIO];
		intel_pt_print_info(&auxtrace_info->priv[0],
				    INTEL_PT_MAX_NONTURBO_RATIO,
				    INTEL_PT_MAX_NONTURBO_RATIO);
	}

	info = &auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN] + 1;
	info_end = (void *)info + auxtrace_info->header.size;

	if (intel_pt_has(auxtrace_info, INTEL_PT_FILTER_STR_LEN)) {
		size_t len;

		len = auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN];
		intel_pt_print_info(&auxtrace_info->priv[0],
				    INTEL_PT_FILTER_STR_LEN,
				    INTEL_PT_FILTER_STR_LEN);
		if (len) {
			const char *filter = (const char *)info;

			len = roundup(len + 1, 8);
			info += len >> 3;
			if ((void *)info > info_end) {
				pr_err("%s: bad filter string length\n", __func__);
				err = -EINVAL;
				goto err_free_queues;
			}
			pt->filter = memdup(filter, len);
			if (!pt->filter) {
				err = -ENOMEM;
				goto err_free_queues;
			}
			if (session->header.needs_swap)
				mem_bswap_64(pt->filter, len);
			if (pt->filter[len - 1]) {
				pr_err("%s: filter string not null terminated\n", __func__);
				err = -EINVAL;
				goto err_free_queues;
			}
			err = addr_filters__parse_bare_filter(&pt->filts,
							      filter);
			if (err)
				goto err_free_queues;
		}
		intel_pt_print_info_str("Filter string", pt->filter);
	}

	pt->timeless_decoding = intel_pt_timeless_decoding(pt);
	pt->have_tsc = intel_pt_have_tsc(pt);
	pt->sampling_mode = false;
	pt->est_tsc = !pt->timeless_decoding;

	pt->unknown_thread = thread__new(999999999, 999999999);
	if (!pt->unknown_thread) {
		err = -ENOMEM;
		goto err_free_queues;
	}

	/*
	 * Since this thread will not be kept in any rbtree not in a
	 * list, initialize its list node so that at thread__put() the
	 * current thread lifetime assuption is kept and we don't segfault
	 * at list_del_init().
	 */
	INIT_LIST_HEAD(&pt->unknown_thread->node);

	err = thread__set_comm(pt->unknown_thread, "unknown", 0);
	if (err)
		goto err_delete_thread;
	if (thread__init_map_groups(pt->unknown_thread, pt->machine)) {
		err = -ENOMEM;
		goto err_delete_thread;
	}

	pt->auxtrace.process_event = intel_pt_process_event;
	pt->auxtrace.process_auxtrace_event = intel_pt_process_auxtrace_event;
	pt->auxtrace.flush_events = intel_pt_flush;
	pt->auxtrace.free_events = intel_pt_free_events;
	pt->auxtrace.free = intel_pt_free;
	session->auxtrace = &pt->auxtrace;

	if (dump_trace)
		return 0;

	if (pt->have_sched_switch == 1) {
		pt->switch_evsel = intel_pt_find_sched_switch(session->evlist);
		if (!pt->switch_evsel) {
			pr_err("%s: missing sched_switch event\n", __func__);
			err = -EINVAL;
			goto err_delete_thread;
		}
	} else if (pt->have_sched_switch == 2 &&
		   !intel_pt_find_switch(session->evlist)) {
		pr_err("%s: missing context_switch attribute flag\n", __func__);
		err = -EINVAL;
		goto err_delete_thread;
	}

	if (session->itrace_synth_opts && session->itrace_synth_opts->set) {
		pt->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&pt->synth_opts);
		if (use_browser != -1) {
			pt->synth_opts.branches = false;
			pt->synth_opts.callchain = true;
		}
		if (session->itrace_synth_opts)
			pt->synth_opts.thread_stack =
				session->itrace_synth_opts->thread_stack;
	}

	if (pt->synth_opts.log)
		intel_pt_log_enable();

	/* Maximum non-turbo ratio is TSC freq / 100 MHz */
	if (pt->tc.time_mult) {
		u64 tsc_freq = intel_pt_ns_to_ticks(pt, 1000000000);

		if (!pt->max_non_turbo_ratio)
			pt->max_non_turbo_ratio =
					(tsc_freq + 50000000) / 100000000;
		intel_pt_log("TSC frequency %"PRIu64"\n", tsc_freq);
		intel_pt_log("Maximum non-turbo ratio %u\n",
			     pt->max_non_turbo_ratio);
	}

	if (pt->synth_opts.calls)
		pt->branches_filter |= PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC |
				       PERF_IP_FLAG_TRACE_END;
	if (pt->synth_opts.returns)
		pt->branches_filter |= PERF_IP_FLAG_RETURN |
				       PERF_IP_FLAG_TRACE_BEGIN;

	if (pt->synth_opts.callchain && !symbol_conf.use_callchain) {
		symbol_conf.use_callchain = true;
		if (callchain_register_param(&callchain_param) < 0) {
			symbol_conf.use_callchain = false;
			pt->synth_opts.callchain = false;
		}
	}

	err = intel_pt_synth_events(pt, session);
	if (err)
		goto err_delete_thread;

	err = auxtrace_queues__process_index(&pt->queues, session);
	if (err)
		goto err_delete_thread;

	if (pt->queues.populated)
		pt->data_queued = true;

	if (pt->timeless_decoding)
		pr_debug2("Intel PT decoding without timestamps\n");

	return 0;

err_delete_thread:
	thread__zput(pt->unknown_thread);
err_free_queues:
	intel_pt_log_disable();
	auxtrace_queues__free(&pt->queues);
	session->auxtrace = NULL;
err_free:
	addr_filters__exit(&pt->filts);
	zfree(&pt->filter);
	free(pt);
	return err;
}

/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/bpf_perf_event.h>
#include <uapi/linux/perf_event.h>
#include "bpf_helpers.h"

struct key_t {
	char comm[TASK_COMM_LEN];
	u32 kernstack;
	u32 userstack;
};

struct bpf_map_def SEC("maps") counts = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(struct key_t),
	.value_size = sizeof(u64),
	.max_entries = 10000,
};

struct bpf_map_def SEC("maps") stackmap = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.key_size = sizeof(u32),
	.value_size = PERF_MAX_STACK_DEPTH * sizeof(u64),
	.max_entries = 10000,
};

#define KERN_STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP)
#define USER_STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP | BPF_F_USER_STACK)

SEC("perf_event")
int bpf_prog1(struct bpf_perf_event_data *ctx)
{
	char fmt[] = "CPU-%d period %lld ip %llx";
	u32 cpu = bpf_get_smp_processor_id();
	struct key_t key;
	u64 *val, one = 1;

	if (ctx->sample_period < 10000)
		/* ignore warmup */
		return 0;
	bpf_get_current_comm(&key.comm, sizeof(key.comm));
	key.kernstack = bpf_get_stackid(ctx, &stackmap, KERN_STACKID_FLAGS);
	key.userstack = bpf_get_stackid(ctx, &stackmap, USER_STACKID_FLAGS);
	if ((int)key.kernstack < 0 && (int)key.userstack < 0) {
		bpf_trace_printk(fmt, sizeof(fmt), cpu, ctx->sample_period,
				 PT_REGS_IP(&ctx->regs));
		return 0;
	}

	val = bpf_map_lookup_elem(&counts, &key);
	if (val)
		(*val)++;
	else
		bpf_map_update_elem(&counts, &key, &one, BPF_NOEXIST);
	return 0;
}

char _license[] SEC("license") = "GPL";

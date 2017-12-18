/* Copyright (c) 2011-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/bpf_perf_event.h>
#include <linux/filter.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include "trace.h"

/**
 * trace_call_bpf - invoke BPF program
 * @prog: BPF program
 * @ctx: opaque context pointer
 *
 * kprobe handlers execute BPF programs via this helper.
 * Can be used from static tracepoints in the future.
 *
 * Return: BPF programs always return an integer which is interpreted by
 * kprobe handler as:
 * 0 - return from kprobe (event is filtered out)
 * 1 - store kprobe event into ring buffer
 * Other values are reserved and currently alias to 1
 */
unsigned int trace_call_bpf(struct bpf_prog *prog, void *ctx)
{
	unsigned int ret;

	if (in_nmi()) /* not supported yet */
		return 1;

	preempt_disable();

	if (unlikely(__this_cpu_inc_return(bpf_prog_active) != 1)) {
		/*
		 * since some bpf program is already running on this cpu,
		 * don't call into another bpf program (same or different)
		 * and don't send kprobe event into ring-buffer,
		 * so return zero here
		 */
		ret = 0;
		goto out;
	}

	rcu_read_lock();
	ret = BPF_PROG_RUN(prog, ctx);
	rcu_read_unlock();

 out:
	__this_cpu_dec(bpf_prog_active);
	preempt_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(trace_call_bpf);

BPF_CALL_3(bpf_probe_read, void *, dst, u32, size, const void *, unsafe_ptr)
{
	int ret;

	ret = probe_kernel_read(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);

	return ret;
}

static const struct bpf_func_proto bpf_probe_read_proto = {
	.func		= bpf_probe_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_RAW_STACK,
	.arg2_type	= ARG_CONST_STACK_SIZE,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_probe_write_user, void *, unsafe_ptr, const void *, src,
	   u32, size)
{
	/*
	 * Ensure we're in user context which is safe for the helper to
	 * run. This helper has no business in a kthread.
	 *
	 * access_ok() should prevent writing to non-user memory, but in
	 * some situations (nommu, temporary switch, etc) access_ok() does
	 * not provide enough validation, hence the check on KERNEL_DS.
	 */

	if (unlikely(in_interrupt() ||
		     current->flags & (PF_KTHREAD | PF_EXITING)))
		return -EPERM;
	if (unlikely(segment_eq(get_fs(), KERNEL_DS)))
		return -EPERM;
	if (!access_ok(VERIFY_WRITE, unsafe_ptr, size))
		return -EPERM;

	return probe_kernel_write(unsafe_ptr, src, size);
}

static const struct bpf_func_proto bpf_probe_write_user_proto = {
	.func		= bpf_probe_write_user,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_PTR_TO_STACK,
	.arg3_type	= ARG_CONST_STACK_SIZE,
};

static const struct bpf_func_proto *bpf_get_probe_write_proto(void)
{
	pr_warn_ratelimited("%s[%d] is installing a program with bpf_probe_write_user helper that may corrupt user memory!",
			    current->comm, task_pid_nr(current));

	return &bpf_probe_write_user_proto;
}

/*
 * limited trace_printk()
 * only %d %u %x %ld %lu %lx %lld %llu %llx %p %s conversion specifiers allowed
 */
BPF_CALL_5(bpf_trace_printk, char *, fmt, u32, fmt_size, u64, arg1,
	   u64, arg2, u64, arg3)
{
	bool str_seen = false;
	int mod[3] = {};
	int fmt_cnt = 0;
	u64 unsafe_addr;
	char buf[64];
	int i;

	/*
	 * bpf_check()->check_func_arg()->check_stack_boundary()
	 * guarantees that fmt points to bpf program stack,
	 * fmt_size bytes of it were initialized and fmt_size > 0
	 */
	if (fmt[--fmt_size] != 0)
		return -EINVAL;

	/* check format string for allowed specifiers */
	for (i = 0; i < fmt_size; i++) {
		if ((!isprint(fmt[i]) && !isspace(fmt[i])) || !isascii(fmt[i]))
			return -EINVAL;

		if (fmt[i] != '%')
			continue;

		if (fmt_cnt >= 3)
			return -EINVAL;

		/* fmt[i] != 0 && fmt[last] == 0, so we can access fmt[i + 1] */
		i++;
		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		} else if (fmt[i] == 'p' || fmt[i] == 's') {
			mod[fmt_cnt]++;
			i++;
			if (!isspace(fmt[i]) && !ispunct(fmt[i]) && fmt[i] != 0)
				return -EINVAL;
			fmt_cnt++;
			if (fmt[i - 1] == 's') {
				if (str_seen)
					/* allow only one '%s' per fmt string */
					return -EINVAL;
				str_seen = true;

				switch (fmt_cnt) {
				case 1:
					unsafe_addr = arg1;
					arg1 = (long) buf;
					break;
				case 2:
					unsafe_addr = arg2;
					arg2 = (long) buf;
					break;
				case 3:
					unsafe_addr = arg3;
					arg3 = (long) buf;
					break;
				}
				buf[0] = 0;
				strncpy_from_unsafe(buf,
						    (void *) (long) unsafe_addr,
						    sizeof(buf));
			}
			continue;
		}

		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		}

		if (fmt[i] != 'd' && fmt[i] != 'u' && fmt[i] != 'x')
			return -EINVAL;
		fmt_cnt++;
	}

	return __trace_printk(1/* fake ip will not be printed */, fmt,
			      mod[0] == 2 ? arg1 : mod[0] == 1 ? (long) arg1 : (u32) arg1,
			      mod[1] == 2 ? arg2 : mod[1] == 1 ? (long) arg2 : (u32) arg2,
			      mod[2] == 2 ? arg3 : mod[2] == 1 ? (long) arg3 : (u32) arg3);
}

static const struct bpf_func_proto bpf_trace_printk_proto = {
	.func		= bpf_trace_printk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_STACK,
	.arg2_type	= ARG_CONST_STACK_SIZE,
};

const struct bpf_func_proto *bpf_get_trace_printk_proto(void)
{
	/*
	 * this program might be calling bpf_trace_printk,
	 * so allocate per-cpu printk buffers
	 */
	trace_printk_init_buffers();

	return &bpf_trace_printk_proto;
}

BPF_CALL_2(bpf_perf_event_read, struct bpf_map *, map, u64, flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	unsigned int cpu = smp_processor_id();
	u64 index = flags & BPF_F_INDEX_MASK;
	struct bpf_event_entry *ee;
	struct perf_event *event;

	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (index == BPF_F_CURRENT_CPU)
		index = cpu;
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;

	event = ee->event;
	if (unlikely(event->attr.type != PERF_TYPE_HARDWARE &&
		     event->attr.type != PERF_TYPE_RAW))
		return -EINVAL;

	/* make sure event is local and doesn't have pmu::count */
	if (unlikely(event->oncpu != cpu || event->pmu->count))
		return -EINVAL;

	/*
	 * we don't know if the function is run successfully by the
	 * return value. It can be judged in other places, such as
	 * eBPF programs.
	 */
	return perf_event_read_local(event);
}

static const struct bpf_func_proto bpf_perf_event_read_proto = {
	.func		= bpf_perf_event_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
};

static __always_inline u64
__bpf_perf_event_output(struct pt_regs *regs, struct bpf_map *map,
			u64 flags, struct perf_raw_record *raw)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	unsigned int cpu = smp_processor_id();
	u64 index = flags & BPF_F_INDEX_MASK;
	struct perf_sample_data sample_data;
	struct bpf_event_entry *ee;
	struct perf_event *event;

	if (index == BPF_F_CURRENT_CPU)
		index = cpu;
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;

	event = ee->event;
	if (unlikely(event->attr.type != PERF_TYPE_SOFTWARE ||
		     event->attr.config != PERF_COUNT_SW_BPF_OUTPUT))
		return -EINVAL;

	if (unlikely(event->oncpu != cpu))
		return -EOPNOTSUPP;

	perf_sample_data_init(&sample_data, 0, 0);
	sample_data.raw = raw;
	perf_event_output(event, &sample_data, regs);
	return 0;
}

BPF_CALL_5(bpf_perf_event_output, struct pt_regs *, regs, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	struct perf_raw_record raw = {
		.frag = {
			.size = size,
			.data = data,
		},
	};

	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;

	return __bpf_perf_event_output(regs, map, flags, &raw);
}

static const struct bpf_func_proto bpf_perf_event_output_proto = {
	.func		= bpf_perf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

static DEFINE_PER_CPU(struct pt_regs, bpf_pt_regs);

u64 bpf_event_output(struct bpf_map *map, u64 flags, void *meta, u64 meta_size,
		     void *ctx, u64 ctx_size, bpf_ctx_copy_t ctx_copy)
{
	struct pt_regs *regs = this_cpu_ptr(&bpf_pt_regs);
	struct perf_raw_frag frag = {
		.copy		= ctx_copy,
		.size		= ctx_size,
		.data		= ctx,
	};
	struct perf_raw_record raw = {
		.frag = {
			{
				.next	= ctx_size ? &frag : NULL,
			},
			.size	= meta_size,
			.data	= meta,
		},
	};

	perf_fetch_caller_regs(regs);

	return __bpf_perf_event_output(regs, map, flags, &raw);
}

BPF_CALL_0(bpf_get_current_task)
{
	return (long) current;
}

static const struct bpf_func_proto bpf_get_current_task_proto = {
	.func		= bpf_get_current_task,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_2(bpf_current_task_under_cgroup, struct bpf_map *, map, u32, idx)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct cgroup *cgrp;

	if (unlikely(in_interrupt()))
		return -EINVAL;
	if (unlikely(idx >= array->map.max_entries))
		return -E2BIG;

	cgrp = READ_ONCE(array->ptrs[idx]);
	if (unlikely(!cgrp))
		return -EAGAIN;

	return task_under_cgroup_hierarchy(current, cgrp);
}

static const struct bpf_func_proto bpf_current_task_under_cgroup_proto = {
	.func           = bpf_current_task_under_cgroup,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_CONST_MAP_PTR,
	.arg2_type      = ARG_ANYTHING,
};

static const struct bpf_func_proto *tracing_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_probe_read:
		return &bpf_probe_read_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_perf_event_read:
		return &bpf_perf_event_read_proto;
	case BPF_FUNC_probe_write_user:
		return bpf_get_probe_write_proto();
	case BPF_FUNC_current_task_under_cgroup:
		return &bpf_current_task_under_cgroup_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *kprobe_prog_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto;
	default:
		return tracing_func_proto(func_id);
	}
}

/* bpf+kprobe programs can access fields of 'struct pt_regs' */
static bool kprobe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
					enum bpf_reg_type *reg_type)
{
	if (off < 0 || off >= sizeof(struct pt_regs))
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

static const struct bpf_verifier_ops kprobe_prog_ops = {
	.get_func_proto  = kprobe_prog_func_proto,
	.is_valid_access = kprobe_prog_is_valid_access,
};

static struct bpf_prog_type_list kprobe_tl = {
	.ops	= &kprobe_prog_ops,
	.type	= BPF_PROG_TYPE_KPROBE,
};

BPF_CALL_5(bpf_perf_event_output_tp, void *, tp_buff, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

	/*
	 * r1 points to perf tracepoint buffer where first 8 bytes are hidden
	 * from bpf program and contain a pointer to 'struct pt_regs'. Fetch it
	 * from there and call the same bpf_perf_event_output() helper inline.
	 */
	return ____bpf_perf_event_output(regs, map, flags, data, size);
}

static const struct bpf_func_proto bpf_perf_event_output_proto_tp = {
	.func		= bpf_perf_event_output_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

BPF_CALL_3(bpf_get_stackid_tp, void *, tp_buff, struct bpf_map *, map,
	   u64, flags)
{
	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

	/*
	 * Same comment as in bpf_perf_event_output_tp(), only that this time
	 * the other helper's function body cannot be inlined due to being
	 * external, thus we need to call raw helper function.
	 */
	return bpf_get_stackid((unsigned long) regs, (unsigned long) map,
			       flags, 0, 0);
}

static const struct bpf_func_proto bpf_get_stackid_proto_tp = {
	.func		= bpf_get_stackid_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *tp_prog_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_tp;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto_tp;
	default:
		return tracing_func_proto(func_id);
	}
}

static bool tp_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    enum bpf_reg_type *reg_type)
{
	if (off < sizeof(void *) || off >= PERF_MAX_TRACE_SIZE)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

static const struct bpf_verifier_ops tracepoint_prog_ops = {
	.get_func_proto  = tp_prog_func_proto,
	.is_valid_access = tp_prog_is_valid_access,
};

static struct bpf_prog_type_list tracepoint_tl = {
	.ops	= &tracepoint_prog_ops,
	.type	= BPF_PROG_TYPE_TRACEPOINT,
};

static bool pe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    enum bpf_reg_type *reg_type)
{
	if (off < 0 || off >= sizeof(struct bpf_perf_event_data))
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	if (off == offsetof(struct bpf_perf_event_data, sample_period)) {
		if (size != sizeof(u64))
			return false;
	} else {
		if (size != sizeof(long))
			return false;
	}
	return true;
}

static u32 pe_prog_convert_ctx_access(enum bpf_access_type type, int dst_reg,
				      int src_reg, int ctx_off,
				      struct bpf_insn *insn_buf,
				      struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	switch (ctx_off) {
	case offsetof(struct bpf_perf_event_data, sample_period):
		BUILD_BUG_ON(FIELD_SIZEOF(struct perf_sample_data, period) != sizeof(u64));

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
						       data), dst_reg, src_reg,
				      offsetof(struct bpf_perf_event_data_kern, data));
		*insn++ = BPF_LDX_MEM(BPF_DW, dst_reg, dst_reg,
				      offsetof(struct perf_sample_data, period));
		break;
	default:
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
						       regs), dst_reg, src_reg,
				      offsetof(struct bpf_perf_event_data_kern, regs));
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(long), dst_reg, dst_reg, ctx_off);
		break;
	}

	return insn - insn_buf;
}

static const struct bpf_verifier_ops perf_event_prog_ops = {
	.get_func_proto		= tp_prog_func_proto,
	.is_valid_access	= pe_prog_is_valid_access,
	.convert_ctx_access	= pe_prog_convert_ctx_access,
};

static struct bpf_prog_type_list perf_event_tl = {
	.ops	= &perf_event_prog_ops,
	.type	= BPF_PROG_TYPE_PERF_EVENT,
};

static int __init register_kprobe_prog_ops(void)
{
	bpf_register_prog_type(&kprobe_tl);
	bpf_register_prog_type(&tracepoint_tl);
	bpf_register_prog_type(&perf_event_tl);
	return 0;
}
late_initcall(register_kprobe_prog_ops);

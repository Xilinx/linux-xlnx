/*
 * thread-stack.h: Synthesize a thread's stack using call / return events
 * Copyright (c) 2014, Intel Corporation.
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

#ifndef __PERF_THREAD_STACK_H
#define __PERF_THREAD_STACK_H

#include <sys/types.h>

#include <linux/types.h>

struct thread;
struct comm;
struct ip_callchain;
struct symbol;
struct dso;
struct comm;
struct perf_sample;
struct addr_location;
struct call_path;

/*
 * Call/Return flags.
 *
 * CALL_RETURN_NO_CALL: 'return' but no matching 'call'
 * CALL_RETURN_NO_RETURN: 'call' but no matching 'return'
 */
enum {
	CALL_RETURN_NO_CALL	= 1 << 0,
	CALL_RETURN_NO_RETURN	= 1 << 1,
};

/**
 * struct call_return - paired call/return information.
 * @thread: thread in which call/return occurred
 * @comm: comm in which call/return occurred
 * @cp: call path
 * @call_time: timestamp of call (if known)
 * @return_time: timestamp of return (if known)
 * @branch_count: number of branches seen between call and return
 * @call_ref: external reference to 'call' sample (e.g. db_id)
 * @return_ref:  external reference to 'return' sample (e.g. db_id)
 * @db_id: id used for db-export
 * @flags: Call/Return flags
 */
struct call_return {
	struct thread *thread;
	struct comm *comm;
	struct call_path *cp;
	u64 call_time;
	u64 return_time;
	u64 branch_count;
	u64 call_ref;
	u64 return_ref;
	u64 db_id;
	u32 flags;
};

/**
 * struct call_return_processor - provides a call-back to consume call-return
 *                                information.
 * @cpr: call path root
 * @process: call-back that accepts call/return information
 * @data: anonymous data for call-back
 */
struct call_return_processor {
	struct call_path_root *cpr;
	int (*process)(struct call_return *cr, void *data);
	void *data;
};

int thread_stack__event(struct thread *thread, u32 flags, u64 from_ip,
			u64 to_ip, u16 insn_len, u64 trace_nr);
void thread_stack__set_trace_nr(struct thread *thread, u64 trace_nr);
void thread_stack__sample(struct thread *thread, struct ip_callchain *chain,
			  size_t sz, u64 ip);
int thread_stack__flush(struct thread *thread);
void thread_stack__free(struct thread *thread);
size_t thread_stack__depth(struct thread *thread);

struct call_return_processor *
call_return_processor__new(int (*process)(struct call_return *cr, void *data),
			   void *data);
void call_return_processor__free(struct call_return_processor *crp);
int thread_stack__process(struct thread *thread, struct comm *comm,
			  struct perf_sample *sample,
			  struct addr_location *from_al,
			  struct addr_location *to_al, u64 ref,
			  struct call_return_processor *crp);

#endif

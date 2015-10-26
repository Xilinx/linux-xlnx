#undef TRACE_SYSTEM
#define TRACE_SYSTEM hist

#if !defined(_TRACE_HIST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HIST_H

#include "latency_hist.h"
#include <linux/tracepoint.h>

#if !defined(CONFIG_PREEMPT_OFF_HIST) && !defined(CONFIG_INTERRUPT_OFF_HIST)
#define trace_preemptirqsoff_hist(a, b)
#else
TRACE_EVENT(preemptirqsoff_hist,

	TP_PROTO(int reason, int starthist),

	TP_ARGS(reason, starthist),

	TP_STRUCT__entry(
		__field(int,	reason)
		__field(int,	starthist)
	),

	TP_fast_assign(
		__entry->reason		= reason;
		__entry->starthist	= starthist;
	),

	TP_printk("reason=%s starthist=%s", getaction(__entry->reason),
		  __entry->starthist ? "start" : "stop")
);
#endif

#ifndef CONFIG_MISSED_TIMER_OFFSETS_HIST
#define trace_hrtimer_interrupt(a, b, c, d)
#else
TRACE_EVENT(hrtimer_interrupt,

	TP_PROTO(int cpu, long long offset, struct task_struct *curr,
		struct task_struct *task),

	TP_ARGS(cpu, offset, curr, task),

	TP_STRUCT__entry(
		__field(int,		cpu)
		__field(long long,	offset)
		__array(char,		ccomm,	TASK_COMM_LEN)
		__field(int,		cprio)
		__array(char,		tcomm,	TASK_COMM_LEN)
		__field(int,		tprio)
	),

	TP_fast_assign(
		__entry->cpu	= cpu;
		__entry->offset	= offset;
		memcpy(__entry->ccomm, curr->comm, TASK_COMM_LEN);
		__entry->cprio  = curr->prio;
		memcpy(__entry->tcomm, task != NULL ? task->comm : "<none>",
			task != NULL ? TASK_COMM_LEN : 7);
		__entry->tprio  = task != NULL ? task->prio : -1;
	),

	TP_printk("cpu=%d offset=%lld curr=%s[%d] thread=%s[%d]",
		__entry->cpu, __entry->offset, __entry->ccomm,
		__entry->cprio, __entry->tcomm, __entry->tprio)
);
#endif

#endif /* _TRACE_HIST_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

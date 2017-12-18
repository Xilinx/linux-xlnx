#if !defined(_GPU_SCHED_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GPU_SCHED_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_sched
#define TRACE_INCLUDE_FILE gpu_sched_trace

TRACE_EVENT(amd_sched_job,
	    TP_PROTO(struct amd_sched_job *sched_job),
	    TP_ARGS(sched_job),
	    TP_STRUCT__entry(
			     __field(struct amd_sched_entity *, entity)
			     __field(struct amd_sched_job *, sched_job)
			     __field(struct fence *, fence)
			     __field(const char *, name)
			     __field(u32, job_count)
			     __field(int, hw_job_count)
			     ),

	    TP_fast_assign(
			   __entry->entity = sched_job->s_entity;
			   __entry->sched_job = sched_job;
			   __entry->fence = &sched_job->s_fence->finished;
			   __entry->name = sched_job->sched->name;
			   __entry->job_count = kfifo_len(
				   &sched_job->s_entity->job_queue) / sizeof(sched_job);
			   __entry->hw_job_count = atomic_read(
				   &sched_job->sched->hw_rq_count);
			   ),
	    TP_printk("entity=%p, sched job=%p, fence=%p, ring=%s, job count:%u, hw job count:%d",
		      __entry->entity, __entry->sched_job, __entry->fence, __entry->name,
		      __entry->job_count, __entry->hw_job_count)
);

TRACE_EVENT(amd_sched_process_job,
	    TP_PROTO(struct amd_sched_fence *fence),
	    TP_ARGS(fence),
	    TP_STRUCT__entry(
		    __field(struct fence *, fence)
		    ),

	    TP_fast_assign(
		    __entry->fence = &fence->finished;
		    ),
	    TP_printk("fence=%p signaled", __entry->fence)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

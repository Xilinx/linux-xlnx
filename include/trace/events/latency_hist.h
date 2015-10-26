#ifndef _LATENCY_HIST_H
#define _LATENCY_HIST_H

enum hist_action {
	IRQS_ON,
	PREEMPT_ON,
	TRACE_STOP,
	IRQS_OFF,
	PREEMPT_OFF,
	TRACE_START,
};

static char *actions[] = {
	"IRQS_ON",
	"PREEMPT_ON",
	"TRACE_STOP",
	"IRQS_OFF",
	"PREEMPT_OFF",
	"TRACE_START",
};

static inline char *getaction(int action)
{
	if (action >= 0 && action <= sizeof(actions)/sizeof(actions[0]))
		return actions[action];
	return "unknown";
}

#endif /* _LATENCY_HIST_H */

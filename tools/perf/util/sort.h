#ifndef __PERF_SORT_H
#define __PERF_SORT_H
#include "../builtin.h"

#include "util.h"

#include "color.h"
#include <linux/list.h>
#include "cache.h"
#include <linux/rbtree.h>
#include "symbol.h"
#include "string.h"
#include "callchain.h"
#include "strlist.h"
#include "values.h"

#include "../perf.h"
#include "debug.h"
#include "header.h"

#include <subcmd/parse-options.h>
#include "parse-events.h"
#include "hist.h"
#include "thread.h"

extern regex_t parent_regex;
extern const char *sort_order;
extern const char *field_order;
extern const char default_parent_pattern[];
extern const char *parent_pattern;
extern const char *default_sort_order;
extern regex_t ignore_callees_regex;
extern int have_ignore_callees;
extern enum sort_mode sort__mode;
extern struct sort_entry sort_comm;
extern struct sort_entry sort_dso;
extern struct sort_entry sort_sym;
extern struct sort_entry sort_parent;
extern struct sort_entry sort_dso_from;
extern struct sort_entry sort_dso_to;
extern struct sort_entry sort_sym_from;
extern struct sort_entry sort_sym_to;
extern struct sort_entry sort_srcline;
extern enum sort_type sort__first_dimension;
extern const char default_mem_sort_order[];

struct he_stat {
	u64			period;
	u64			period_sys;
	u64			period_us;
	u64			period_guest_sys;
	u64			period_guest_us;
	u64			weight;
	u32			nr_events;
};

struct hist_entry_diff {
	bool	computed;
	union {
		/* PERF_HPP__DELTA */
		double	period_ratio_delta;

		/* PERF_HPP__RATIO */
		double	period_ratio;

		/* HISTC_WEIGHTED_DIFF */
		s64	wdiff;
	};
};

struct hist_entry_ops {
	void	*(*new)(size_t size);
	void	(*free)(void *ptr);
};

/**
 * struct hist_entry - histogram entry
 *
 * @row_offset - offset from the first callchain expanded to appear on screen
 * @nr_rows - rows expanded in callchain, recalculated on folding/unfolding
 */
struct hist_entry {
	struct rb_node		rb_node_in;
	struct rb_node		rb_node;
	union {
		struct list_head node;
		struct list_head head;
	} pairs;
	struct he_stat		stat;
	struct he_stat		*stat_acc;
	struct map_symbol	ms;
	struct thread		*thread;
	struct comm		*comm;
	u64			ip;
	u64			transaction;
	s32			socket;
	s32			cpu;
	u8			cpumode;
	u8			depth;

	/* We are added by hists__add_dummy_entry. */
	bool			dummy;
	bool			leaf;

	char			level;
	u8			filtered;
	union {
		/*
		 * Since perf diff only supports the stdio output, TUI
		 * fields are only accessed from perf report (or perf
		 * top).  So make it an union to reduce memory usage.
		 */
		struct hist_entry_diff	diff;
		struct /* for TUI */ {
			u16	row_offset;
			u16	nr_rows;
			bool	init_have_children;
			bool	unfolded;
			bool	has_children;
			bool	has_no_entry;
		};
	};
	char			*srcline;
	char			*srcfile;
	struct symbol		*parent;
	struct branch_info	*branch_info;
	struct hists		*hists;
	struct mem_info		*mem_info;
	void			*raw_data;
	u32			raw_size;
	void			*trace_output;
	struct perf_hpp_list	*hpp_list;
	struct hist_entry	*parent_he;
	struct hist_entry_ops	*ops;
	union {
		/* this is for hierarchical entry structure */
		struct {
			struct rb_root	hroot_in;
			struct rb_root  hroot_out;
		};				/* non-leaf entries */
		struct rb_root	sorted_chain;	/* leaf entry has callchains */
	};
	struct callchain_root	callchain[0]; /* must be last member */
};

static inline bool hist_entry__has_pairs(struct hist_entry *he)
{
	return !list_empty(&he->pairs.node);
}

static inline struct hist_entry *hist_entry__next_pair(struct hist_entry *he)
{
	if (hist_entry__has_pairs(he))
		return list_entry(he->pairs.node.next, struct hist_entry, pairs.node);
	return NULL;
}

static inline void hist_entry__add_pair(struct hist_entry *pair,
					struct hist_entry *he)
{
	list_add_tail(&pair->pairs.node, &he->pairs.head);
}

static inline float hist_entry__get_percent_limit(struct hist_entry *he)
{
	u64 period = he->stat.period;
	u64 total_period = hists__total_period(he->hists);

	if (unlikely(total_period == 0))
		return 0;

	if (symbol_conf.cumulate_callchain)
		period = he->stat_acc->period;

	return period * 100.0 / total_period;
}

static inline u64 cl_address(u64 address)
{
	/* return the cacheline of the address */
	return (address & ~(cacheline_size - 1));
}

static inline u64 cl_offset(u64 address)
{
	/* return the cacheline of the address */
	return (address & (cacheline_size - 1));
}

enum sort_mode {
	SORT_MODE__NORMAL,
	SORT_MODE__BRANCH,
	SORT_MODE__MEMORY,
	SORT_MODE__TOP,
	SORT_MODE__DIFF,
	SORT_MODE__TRACEPOINT,
};

enum sort_type {
	/* common sort keys */
	SORT_PID,
	SORT_COMM,
	SORT_DSO,
	SORT_SYM,
	SORT_PARENT,
	SORT_CPU,
	SORT_SOCKET,
	SORT_SRCLINE,
	SORT_SRCFILE,
	SORT_LOCAL_WEIGHT,
	SORT_GLOBAL_WEIGHT,
	SORT_TRANSACTION,
	SORT_TRACE,

	/* branch stack specific sort keys */
	__SORT_BRANCH_STACK,
	SORT_DSO_FROM = __SORT_BRANCH_STACK,
	SORT_DSO_TO,
	SORT_SYM_FROM,
	SORT_SYM_TO,
	SORT_MISPREDICT,
	SORT_ABORT,
	SORT_IN_TX,
	SORT_CYCLES,
	SORT_SRCLINE_FROM,
	SORT_SRCLINE_TO,

	/* memory mode specific sort keys */
	__SORT_MEMORY_MODE,
	SORT_MEM_DADDR_SYMBOL = __SORT_MEMORY_MODE,
	SORT_MEM_DADDR_DSO,
	SORT_MEM_LOCKED,
	SORT_MEM_TLB,
	SORT_MEM_LVL,
	SORT_MEM_SNOOP,
	SORT_MEM_DCACHELINE,
	SORT_MEM_IADDR_SYMBOL,
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	const char *se_header;

	int64_t (*se_cmp)(struct hist_entry *, struct hist_entry *);
	int64_t (*se_collapse)(struct hist_entry *, struct hist_entry *);
	int64_t	(*se_sort)(struct hist_entry *, struct hist_entry *);
	int	(*se_snprintf)(struct hist_entry *he, char *bf, size_t size,
			       unsigned int width);
	int	(*se_filter)(struct hist_entry *he, int type, const void *arg);
	u8	se_width_idx;
};

extern struct sort_entry sort_thread;
extern struct list_head hist_entry__sort_list;

struct perf_evlist;
struct pevent;
int setup_sorting(struct perf_evlist *evlist);
int setup_output_field(void);
void reset_output_field(void);
void sort__setup_elide(FILE *fp);
void perf_hpp__set_elide(int idx, bool elide);

int report_parse_ignore_callees_opt(const struct option *opt, const char *arg, int unset);

bool is_strict_order(const char *order);

int hpp_dimension__add_output(unsigned col);
void reset_dimensions(void);
int sort_dimension__add(struct perf_hpp_list *list, const char *tok,
			struct perf_evlist *evlist,
			int level);
int output_field_add(struct perf_hpp_list *list, char *tok);
int64_t
sort__iaddr_cmp(struct hist_entry *left, struct hist_entry *right);
int64_t
sort__daddr_cmp(struct hist_entry *left, struct hist_entry *right);
int64_t
sort__dcacheline_cmp(struct hist_entry *left, struct hist_entry *right);
#endif	/* __PERF_SORT_H */

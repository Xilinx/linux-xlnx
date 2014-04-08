#include <stdio.h>
#include "../libslang.h"
#include <stdlib.h>
#include <string.h>
#include <linux/rbtree.h>

#include "../../util/evsel.h"
#include "../../util/evlist.h"
#include "../../util/hist.h"
#include "../../util/pstack.h"
#include "../../util/sort.h"
#include "../../util/util.h"
#include "../../arch/common.h"

#include "../browser.h"
#include "../helpline.h"
#include "../util.h"
#include "../ui.h"
#include "map.h"

struct hist_browser {
	struct ui_browser   b;
	struct hists	    *hists;
	struct hist_entry   *he_selection;
	struct map_symbol   *selection;
	int		     print_seq;
	bool		     show_dso;
	float		     min_pcnt;
	u64		     nr_pcnt_entries;
};

extern void hist_browser__init_hpp(void);

static int hists__browser_title(struct hists *hists, char *bf, size_t size,
				const char *ev_name);

static void hist_browser__refresh_dimensions(struct hist_browser *browser)
{
	/* 3 == +/- toggle symbol before actual hist_entry rendering */
	browser->b.width = 3 + (hists__sort_list_width(browser->hists) +
			     sizeof("[k]"));
}

static void hist_browser__reset(struct hist_browser *browser)
{
	browser->b.nr_entries = browser->hists->nr_entries;
	hist_browser__refresh_dimensions(browser);
	ui_browser__reset_index(&browser->b);
}

static char tree__folded_sign(bool unfolded)
{
	return unfolded ? '-' : '+';
}

static char map_symbol__folded(const struct map_symbol *ms)
{
	return ms->has_children ? tree__folded_sign(ms->unfolded) : ' ';
}

static char hist_entry__folded(const struct hist_entry *he)
{
	return map_symbol__folded(&he->ms);
}

static char callchain_list__folded(const struct callchain_list *cl)
{
	return map_symbol__folded(&cl->ms);
}

static void map_symbol__set_folding(struct map_symbol *ms, bool unfold)
{
	ms->unfolded = unfold ? ms->has_children : false;
}

static int callchain_node__count_rows_rb_tree(struct callchain_node *node)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		char folded_sign = ' '; /* No children */

		list_for_each_entry(chain, &child->val, list) {
			++n;
			/* We need this because we may not have children */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') /* Have children and they're unfolded */
			n += callchain_node__count_rows_rb_tree(child);
	}

	return n;
}

static int callchain_node__count_rows(struct callchain_node *node)
{
	struct callchain_list *chain;
	bool unfolded = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		unfolded = chain->ms.unfolded;
	}

	if (unfolded)
		n += callchain_node__count_rows_rb_tree(node);

	return n;
}

static int callchain__count_rows(struct rb_root *chain)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__count_rows(node);
	}

	return n;
}

static bool map_symbol__toggle_fold(struct map_symbol *ms)
{
	if (!ms)
		return false;

	if (!ms->has_children)
		return false;

	ms->unfolded = !ms->unfolded;
	return true;
}

static void callchain_node__init_have_children_rb_tree(struct callchain_node *node)
{
	struct rb_node *nd = rb_first(&node->rb_root);

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool first = true;

		list_for_each_entry(chain, &child->val, list) {
			if (first) {
				first = false;
				chain->ms.has_children = chain->list.next != &child->val ||
							 !RB_EMPTY_ROOT(&child->rb_root);
			} else
				chain->ms.has_children = chain->list.next == &child->val &&
							 !RB_EMPTY_ROOT(&child->rb_root);
		}

		callchain_node__init_have_children_rb_tree(child);
	}
}

static void callchain_node__init_have_children(struct callchain_node *node)
{
	struct callchain_list *chain;

	list_for_each_entry(chain, &node->val, list)
		chain->ms.has_children = !RB_EMPTY_ROOT(&node->rb_root);

	callchain_node__init_have_children_rb_tree(node);
}

static void callchain__init_have_children(struct rb_root *root)
{
	struct rb_node *nd;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		callchain_node__init_have_children(node);
	}
}

static void hist_entry__init_have_children(struct hist_entry *he)
{
	if (!he->init_have_children) {
		he->ms.has_children = !RB_EMPTY_ROOT(&he->sorted_chain);
		callchain__init_have_children(&he->sorted_chain);
		he->init_have_children = true;
	}
}

static bool hist_browser__toggle_fold(struct hist_browser *browser)
{
	if (map_symbol__toggle_fold(browser->selection)) {
		struct hist_entry *he = browser->he_selection;

		hist_entry__init_have_children(he);
		browser->hists->nr_entries -= he->nr_rows;

		if (he->ms.unfolded)
			he->nr_rows = callchain__count_rows(&he->sorted_chain);
		else
			he->nr_rows = 0;
		browser->hists->nr_entries += he->nr_rows;
		browser->b.nr_entries = browser->hists->nr_entries;

		return true;
	}

	/* If it doesn't have children, no toggling performed */
	return false;
}

static int callchain_node__set_folding_rb_tree(struct callchain_node *node, bool unfold)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&node->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool has_children = false;

		list_for_each_entry(chain, &child->val, list) {
			++n;
			map_symbol__set_folding(&chain->ms, unfold);
			has_children = chain->ms.has_children;
		}

		if (has_children)
			n += callchain_node__set_folding_rb_tree(child, unfold);
	}

	return n;
}

static int callchain_node__set_folding(struct callchain_node *node, bool unfold)
{
	struct callchain_list *chain;
	bool has_children = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		map_symbol__set_folding(&chain->ms, unfold);
		has_children = chain->ms.has_children;
	}

	if (has_children)
		n += callchain_node__set_folding_rb_tree(node, unfold);

	return n;
}

static int callchain__set_folding(struct rb_root *chain, bool unfold)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__set_folding(node, unfold);
	}

	return n;
}

static void hist_entry__set_folding(struct hist_entry *he, bool unfold)
{
	hist_entry__init_have_children(he);
	map_symbol__set_folding(&he->ms, unfold);

	if (he->ms.has_children) {
		int n = callchain__set_folding(&he->sorted_chain, unfold);
		he->nr_rows = unfold ? n : 0;
	} else
		he->nr_rows = 0;
}

static void hists__set_folding(struct hists *hists, bool unfold)
{
	struct rb_node *nd;

	hists->nr_entries = 0;

	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		hist_entry__set_folding(he, unfold);
		hists->nr_entries += 1 + he->nr_rows;
	}
}

static void hist_browser__set_folding(struct hist_browser *browser, bool unfold)
{
	hists__set_folding(browser->hists, unfold);
	browser->b.nr_entries = browser->hists->nr_entries;
	/* Go to the start, we may be way after valid entries after a collapse */
	ui_browser__reset_index(&browser->b);
}

static void ui_browser__warn_lost_events(struct ui_browser *browser)
{
	ui_browser__warning(browser, 4,
		"Events are being lost, check IO/CPU overload!\n\n"
		"You may want to run 'perf' using a RT scheduler policy:\n\n"
		" perf top -r 80\n\n"
		"Or reduce the sampling frequency.");
}

static void hist_browser__update_pcnt_entries(struct hist_browser *hb);

static int hist_browser__run(struct hist_browser *browser, const char *ev_name,
			     struct hist_browser_timer *hbt)
{
	int key;
	char title[160];
	int delay_secs = hbt ? hbt->refresh : 0;

	browser->b.entries = &browser->hists->entries;
	browser->b.nr_entries = browser->hists->nr_entries;
	if (browser->min_pcnt)
		browser->b.nr_entries = browser->nr_pcnt_entries;

	hist_browser__refresh_dimensions(browser);
	hists__browser_title(browser->hists, title, sizeof(title), ev_name);

	if (ui_browser__show(&browser->b, title,
			     "Press '?' for help on key bindings") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&browser->b, delay_secs);

		switch (key) {
		case K_TIMER: {
			u64 nr_entries;
			hbt->timer(hbt->arg);

			if (browser->min_pcnt) {
				hist_browser__update_pcnt_entries(browser);
				nr_entries = browser->nr_pcnt_entries;
			} else {
				nr_entries = browser->hists->nr_entries;
			}

			ui_browser__update_nr_entries(&browser->b, nr_entries);

			if (browser->hists->stats.nr_lost_warned !=
			    browser->hists->stats.nr_events[PERF_RECORD_LOST]) {
				browser->hists->stats.nr_lost_warned =
					browser->hists->stats.nr_events[PERF_RECORD_LOST];
				ui_browser__warn_lost_events(&browser->b);
			}

			hists__browser_title(browser->hists, title, sizeof(title), ev_name);
			ui_browser__show_title(&browser->b, title);
			continue;
		}
		case 'D': { /* Debug */
			static int seq;
			struct hist_entry *h = rb_entry(browser->b.top,
							struct hist_entry, rb_node);
			ui_helpline__pop();
			ui_helpline__fpush("%d: nr_ent=(%d,%d), height=%d, idx=%d, fve: idx=%d, row_off=%d, nrows=%d",
					   seq++, browser->b.nr_entries,
					   browser->hists->nr_entries,
					   browser->b.height,
					   browser->b.index,
					   browser->b.top_idx,
					   h->row_offset, h->nr_rows);
		}
			break;
		case 'C':
			/* Collapse the whole world. */
			hist_browser__set_folding(browser, false);
			break;
		case 'E':
			/* Expand the whole world. */
			hist_browser__set_folding(browser, true);
			break;
		case K_ENTER:
			if (hist_browser__toggle_fold(browser))
				break;
			/* fall thru */
		default:
			goto out;
		}
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

static char *callchain_list__sym_name(struct callchain_list *cl,
				      char *bf, size_t bfsize, bool show_dso)
{
	int printed;

	if (cl->ms.sym)
		printed = scnprintf(bf, bfsize, "%s", cl->ms.sym->name);
	else
		printed = scnprintf(bf, bfsize, "%#" PRIx64, cl->ip);

	if (show_dso)
		scnprintf(bf + printed, bfsize - printed, " %s",
			  cl->ms.map ? cl->ms.map->dso->short_name : "unknown");

	return bf;
}

#define LEVEL_OFFSET_STEP 3

static int hist_browser__show_callchain_node_rb_tree(struct hist_browser *browser,
						     struct callchain_node *chain_node,
						     u64 total, int level,
						     unsigned short row,
						     off_t *row_offset,
						     bool *is_current_entry)
{
	struct rb_node *node;
	int first_row = row, width, offset = level * LEVEL_OFFSET_STEP;
	u64 new_total, remaining;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = chain_node->children_hit;
	else
		new_total = total;

	remaining = new_total;
	node = rb_first(&chain_node->rb_root);
	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		u64 cumul = callchain_cumul_hits(child);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		remaining -= cumul;

		list_for_each_entry(chain, &child->val, list) {
			char bf[1024], *alloc_str;
			const char *str;
			int color;
			bool was_first = first;

			if (first)
				first = false;
			else
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);
			if (*row_offset != 0) {
				--*row_offset;
				goto do_next;
			}

			alloc_str = NULL;
			str = callchain_list__sym_name(chain, bf, sizeof(bf),
						       browser->show_dso);
			if (was_first) {
				double percent = cumul * 100.0 / new_total;

				if (asprintf(&alloc_str, "%2.2f%% %s", percent, str) < 0)
					str = "Not enough memory!";
				else
					str = alloc_str;
			}

			color = HE_COLORSET_NORMAL;
			width = browser->b.width - (offset + extra_offset + 2);
			if (ui_browser__is_current_entry(&browser->b, row)) {
				browser->selection = &chain->ms;
				color = HE_COLORSET_SELECTED;
				*is_current_entry = true;
			}

			ui_browser__set_color(&browser->b, color);
			ui_browser__gotorc(&browser->b, row, 0);
			slsmg_write_nstring(" ", offset + extra_offset);
			slsmg_printf("%c ", folded_sign);
			slsmg_write_nstring(str, width);
			free(alloc_str);

			if (++row == browser->b.height)
				goto out;
do_next:
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') {
			const int new_level = level + (extra_offset ? 2 : 1);
			row += hist_browser__show_callchain_node_rb_tree(browser, child, new_total,
									 new_level, row, row_offset,
									 is_current_entry);
		}
		if (row == browser->b.height)
			goto out;
		node = next;
	}
out:
	return row - first_row;
}

static int hist_browser__show_callchain_node(struct hist_browser *browser,
					     struct callchain_node *node,
					     int level, unsigned short row,
					     off_t *row_offset,
					     bool *is_current_entry)
{
	struct callchain_list *chain;
	int first_row = row,
	     offset = level * LEVEL_OFFSET_STEP,
	     width = browser->b.width - offset;
	char folded_sign = ' ';

	list_for_each_entry(chain, &node->val, list) {
		char bf[1024], *s;
		int color;

		folded_sign = callchain_list__folded(chain);

		if (*row_offset != 0) {
			--*row_offset;
			continue;
		}

		color = HE_COLORSET_NORMAL;
		if (ui_browser__is_current_entry(&browser->b, row)) {
			browser->selection = &chain->ms;
			color = HE_COLORSET_SELECTED;
			*is_current_entry = true;
		}

		s = callchain_list__sym_name(chain, bf, sizeof(bf),
					     browser->show_dso);
		ui_browser__gotorc(&browser->b, row, 0);
		ui_browser__set_color(&browser->b, color);
		slsmg_write_nstring(" ", offset);
		slsmg_printf("%c ", folded_sign);
		slsmg_write_nstring(s, width - 2);

		if (++row == browser->b.height)
			goto out;
	}

	if (folded_sign == '-')
		row += hist_browser__show_callchain_node_rb_tree(browser, node,
								 browser->hists->stats.total_period,
								 level + 1, row,
								 row_offset,
								 is_current_entry);
out:
	return row - first_row;
}

static int hist_browser__show_callchain(struct hist_browser *browser,
					struct rb_root *chain,
					int level, unsigned short row,
					off_t *row_offset,
					bool *is_current_entry)
{
	struct rb_node *nd;
	int first_row = row;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);

		row += hist_browser__show_callchain_node(browser, node, level,
							 row, row_offset,
							 is_current_entry);
		if (row == browser->b.height)
			break;
	}

	return row - first_row;
}

struct hpp_arg {
	struct ui_browser *b;
	char folded_sign;
	bool current_entry;
};

static int __hpp__color_callchain(struct hpp_arg *arg)
{
	if (!symbol_conf.use_callchain)
		return 0;

	slsmg_printf("%c ", arg->folded_sign);
	return 2;
}

static int __hpp__color_fmt(struct perf_hpp *hpp, struct hist_entry *he,
			    u64 (*get_field)(struct hist_entry *),
			    int (*callchain_cb)(struct hpp_arg *))
{
	int ret = 0;
	double percent = 0.0;
	struct hists *hists = he->hists;
	struct hpp_arg *arg = hpp->ptr;

	if (hists->stats.total_period)
		percent = 100.0 * get_field(he) / hists->stats.total_period;

	ui_browser__set_percent_color(arg->b, percent, arg->current_entry);

	if (callchain_cb)
		ret += callchain_cb(arg);

	ret += scnprintf(hpp->buf, hpp->size, "%6.2f%%", percent);
	slsmg_printf("%s", hpp->buf);

	if (symbol_conf.event_group) {
		int prev_idx, idx_delta;
		struct perf_evsel *evsel = hists_to_evsel(hists);
		struct hist_entry *pair;
		int nr_members = evsel->nr_members;

		if (nr_members <= 1)
			goto out;

		prev_idx = perf_evsel__group_idx(evsel);

		list_for_each_entry(pair, &he->pairs.head, pairs.node) {
			u64 period = get_field(pair);
			u64 total = pair->hists->stats.total_period;

			if (!total)
				continue;

			evsel = hists_to_evsel(pair->hists);
			idx_delta = perf_evsel__group_idx(evsel) - prev_idx - 1;

			while (idx_delta--) {
				/*
				 * zero-fill group members in the middle which
				 * have no sample
				 */
				ui_browser__set_percent_color(arg->b, 0.0,
							arg->current_entry);
				ret += scnprintf(hpp->buf, hpp->size,
						 " %6.2f%%", 0.0);
				slsmg_printf("%s", hpp->buf);
			}

			percent = 100.0 * period / total;
			ui_browser__set_percent_color(arg->b, percent,
						      arg->current_entry);
			ret += scnprintf(hpp->buf, hpp->size,
					 " %6.2f%%", percent);
			slsmg_printf("%s", hpp->buf);

			prev_idx = perf_evsel__group_idx(evsel);
		}

		idx_delta = nr_members - prev_idx - 1;

		while (idx_delta--) {
			/*
			 * zero-fill group members at last which have no sample
			 */
			ui_browser__set_percent_color(arg->b, 0.0,
						      arg->current_entry);
			ret += scnprintf(hpp->buf, hpp->size,
					 " %6.2f%%", 0.0);
			slsmg_printf("%s", hpp->buf);
		}
	}
out:
	if (!arg->current_entry || !arg->b->navkeypressed)
		ui_browser__set_color(arg->b, HE_COLORSET_NORMAL);

	return ret;
}

#define __HPP_COLOR_PERCENT_FN(_type, _field, _cb)			\
static u64 __hpp_get_##_field(struct hist_entry *he)			\
{									\
	return he->stat._field;						\
}									\
									\
static int								\
hist_browser__hpp_color_##_type(struct perf_hpp_fmt *fmt __maybe_unused,\
				struct perf_hpp *hpp,			\
				struct hist_entry *he)			\
{									\
	return __hpp__color_fmt(hpp, he, __hpp_get_##_field, _cb);	\
}

__HPP_COLOR_PERCENT_FN(overhead, period, __hpp__color_callchain)
__HPP_COLOR_PERCENT_FN(overhead_sys, period_sys, NULL)
__HPP_COLOR_PERCENT_FN(overhead_us, period_us, NULL)
__HPP_COLOR_PERCENT_FN(overhead_guest_sys, period_guest_sys, NULL)
__HPP_COLOR_PERCENT_FN(overhead_guest_us, period_guest_us, NULL)

#undef __HPP_COLOR_PERCENT_FN

void hist_browser__init_hpp(void)
{
	perf_hpp__init();

	perf_hpp__format[PERF_HPP__OVERHEAD].color =
				hist_browser__hpp_color_overhead;
	perf_hpp__format[PERF_HPP__OVERHEAD_SYS].color =
				hist_browser__hpp_color_overhead_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_US].color =
				hist_browser__hpp_color_overhead_us;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_SYS].color =
				hist_browser__hpp_color_overhead_guest_sys;
	perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_US].color =
				hist_browser__hpp_color_overhead_guest_us;
}

static int hist_browser__show_entry(struct hist_browser *browser,
				    struct hist_entry *entry,
				    unsigned short row)
{
	char s[256];
	int printed = 0;
	int width = browser->b.width;
	char folded_sign = ' ';
	bool current_entry = ui_browser__is_current_entry(&browser->b, row);
	off_t row_offset = entry->row_offset;
	bool first = true;
	struct perf_hpp_fmt *fmt;

	if (current_entry) {
		browser->he_selection = entry;
		browser->selection = &entry->ms;
	}

	if (symbol_conf.use_callchain) {
		hist_entry__init_have_children(entry);
		folded_sign = hist_entry__folded(entry);
	}

	if (row_offset == 0) {
		struct hpp_arg arg = {
			.b 		= &browser->b,
			.folded_sign	= folded_sign,
			.current_entry	= current_entry,
		};
		struct perf_hpp hpp = {
			.buf		= s,
			.size		= sizeof(s),
			.ptr		= &arg,
		};

		ui_browser__gotorc(&browser->b, row, 0);

		perf_hpp__for_each_format(fmt) {
			if (!first) {
				slsmg_printf("  ");
				width -= 2;
			}
			first = false;

			if (fmt->color) {
				width -= fmt->color(fmt, &hpp, entry);
			} else {
				width -= fmt->entry(fmt, &hpp, entry);
				slsmg_printf("%s", s);
			}
		}

		/* The scroll bar isn't being used */
		if (!browser->b.navkeypressed)
			width += 1;

		hist_entry__sort_snprintf(entry, s, sizeof(s), browser->hists);
		slsmg_write_nstring(s, width);
		++row;
		++printed;
	} else
		--row_offset;

	if (folded_sign == '-' && row != browser->b.height) {
		printed += hist_browser__show_callchain(browser, &entry->sorted_chain,
							1, row, &row_offset,
							&current_entry);
		if (current_entry)
			browser->he_selection = entry;
	}

	return printed;
}

static void ui_browser__hists_init_top(struct ui_browser *browser)
{
	if (browser->top == NULL) {
		struct hist_browser *hb;

		hb = container_of(browser, struct hist_browser, b);
		browser->top = rb_first(&hb->hists->entries);
	}
}

static unsigned int hist_browser__refresh(struct ui_browser *browser)
{
	unsigned row = 0;
	struct rb_node *nd;
	struct hist_browser *hb = container_of(browser, struct hist_browser, b);

	ui_browser__hists_init_top(browser);

	for (nd = browser->top; nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = h->stat.period * 100.0 /
					hb->hists->stats.total_period;

		if (h->filtered)
			continue;

		if (percent < hb->min_pcnt)
			continue;

		row += hist_browser__show_entry(hb, h, row);
		if (row == browser->height)
			break;
	}

	return row;
}

static struct rb_node *hists__filter_entries(struct rb_node *nd,
					     struct hists *hists,
					     float min_pcnt)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = h->stat.period * 100.0 /
					hists->stats.total_period;

		if (percent < min_pcnt)
			return NULL;

		if (!h->filtered)
			return nd;

		nd = rb_next(nd);
	}

	return NULL;
}

static struct rb_node *hists__filter_prev_entries(struct rb_node *nd,
						  struct hists *hists,
						  float min_pcnt)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = h->stat.period * 100.0 /
					hists->stats.total_period;

		if (!h->filtered && percent >= min_pcnt)
			return nd;

		nd = rb_prev(nd);
	}

	return NULL;
}

static void ui_browser__hists_seek(struct ui_browser *browser,
				   off_t offset, int whence)
{
	struct hist_entry *h;
	struct rb_node *nd;
	bool first = true;
	struct hist_browser *hb;

	hb = container_of(browser, struct hist_browser, b);

	if (browser->nr_entries == 0)
		return;

	ui_browser__hists_init_top(browser);

	switch (whence) {
	case SEEK_SET:
		nd = hists__filter_entries(rb_first(browser->entries),
					   hb->hists, hb->min_pcnt);
		break;
	case SEEK_CUR:
		nd = browser->top;
		goto do_offset;
	case SEEK_END:
		nd = hists__filter_prev_entries(rb_last(browser->entries),
						hb->hists, hb->min_pcnt);
		first = false;
		break;
	default:
		return;
	}

	/*
	 * Moves not relative to the first visible entry invalidates its
	 * row_offset:
	 */
	h = rb_entry(browser->top, struct hist_entry, rb_node);
	h->row_offset = 0;

	/*
	 * Here we have to check if nd is expanded (+), if it is we can't go
	 * the next top level hist_entry, instead we must compute an offset of
	 * what _not_ to show and not change the first visible entry.
	 *
	 * This offset increments when we are going from top to bottom and
	 * decreases when we're going from bottom to top.
	 *
	 * As we don't have backpointers to the top level in the callchains
	 * structure, we need to always print the whole hist_entry callchain,
	 * skipping the first ones that are before the first visible entry
	 * and stop when we printed enough lines to fill the screen.
	 */
do_offset:
	if (offset > 0) {
		do {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				u16 remaining = h->nr_rows - h->row_offset;
				if (offset > remaining) {
					offset -= remaining;
					h->row_offset = 0;
				} else {
					h->row_offset += offset;
					offset = 0;
					browser->top = nd;
					break;
				}
			}
			nd = hists__filter_entries(rb_next(nd), hb->hists,
						   hb->min_pcnt);
			if (nd == NULL)
				break;
			--offset;
			browser->top = nd;
		} while (offset != 0);
	} else if (offset < 0) {
		while (1) {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				if (first) {
					if (-offset > h->row_offset) {
						offset += h->row_offset;
						h->row_offset = 0;
					} else {
						h->row_offset += offset;
						offset = 0;
						browser->top = nd;
						break;
					}
				} else {
					if (-offset > h->nr_rows) {
						offset += h->nr_rows;
						h->row_offset = 0;
					} else {
						h->row_offset = h->nr_rows + offset;
						offset = 0;
						browser->top = nd;
						break;
					}
				}
			}

			nd = hists__filter_prev_entries(rb_prev(nd), hb->hists,
							hb->min_pcnt);
			if (nd == NULL)
				break;
			++offset;
			browser->top = nd;
			if (offset == 0) {
				/*
				 * Last unfiltered hist_entry, check if it is
				 * unfolded, if it is then we should have
				 * row_offset at its last entry.
				 */
				h = rb_entry(nd, struct hist_entry, rb_node);
				if (h->ms.unfolded)
					h->row_offset = h->nr_rows;
				break;
			}
			first = false;
		}
	} else {
		browser->top = nd;
		h = rb_entry(nd, struct hist_entry, rb_node);
		h->row_offset = 0;
	}
}

static int hist_browser__fprintf_callchain_node_rb_tree(struct hist_browser *browser,
							struct callchain_node *chain_node,
							u64 total, int level,
							FILE *fp)
{
	struct rb_node *node;
	int offset = level * LEVEL_OFFSET_STEP;
	u64 new_total, remaining;
	int printed = 0;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = chain_node->children_hit;
	else
		new_total = total;

	remaining = new_total;
	node = rb_first(&chain_node->rb_root);
	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		u64 cumul = callchain_cumul_hits(child);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		remaining -= cumul;

		list_for_each_entry(chain, &child->val, list) {
			char bf[1024], *alloc_str;
			const char *str;
			bool was_first = first;

			if (first)
				first = false;
			else
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);

			alloc_str = NULL;
			str = callchain_list__sym_name(chain, bf, sizeof(bf),
						       browser->show_dso);
			if (was_first) {
				double percent = cumul * 100.0 / new_total;

				if (asprintf(&alloc_str, "%2.2f%% %s", percent, str) < 0)
					str = "Not enough memory!";
				else
					str = alloc_str;
			}

			printed += fprintf(fp, "%*s%c %s\n", offset + extra_offset, " ", folded_sign, str);
			free(alloc_str);
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') {
			const int new_level = level + (extra_offset ? 2 : 1);
			printed += hist_browser__fprintf_callchain_node_rb_tree(browser, child, new_total,
										new_level, fp);
		}

		node = next;
	}

	return printed;
}

static int hist_browser__fprintf_callchain_node(struct hist_browser *browser,
						struct callchain_node *node,
						int level, FILE *fp)
{
	struct callchain_list *chain;
	int offset = level * LEVEL_OFFSET_STEP;
	char folded_sign = ' ';
	int printed = 0;

	list_for_each_entry(chain, &node->val, list) {
		char bf[1024], *s;

		folded_sign = callchain_list__folded(chain);
		s = callchain_list__sym_name(chain, bf, sizeof(bf), browser->show_dso);
		printed += fprintf(fp, "%*s%c %s\n", offset, " ", folded_sign, s);
	}

	if (folded_sign == '-')
		printed += hist_browser__fprintf_callchain_node_rb_tree(browser, node,
									browser->hists->stats.total_period,
									level + 1,  fp);
	return printed;
}

static int hist_browser__fprintf_callchain(struct hist_browser *browser,
					   struct rb_root *chain, int level, FILE *fp)
{
	struct rb_node *nd;
	int printed = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);

		printed += hist_browser__fprintf_callchain_node(browser, node, level, fp);
	}

	return printed;
}

static int hist_browser__fprintf_entry(struct hist_browser *browser,
				       struct hist_entry *he, FILE *fp)
{
	char s[8192];
	double percent;
	int printed = 0;
	char folded_sign = ' ';

	if (symbol_conf.use_callchain)
		folded_sign = hist_entry__folded(he);

	hist_entry__sort_snprintf(he, s, sizeof(s), browser->hists);
	percent = (he->stat.period * 100.0) / browser->hists->stats.total_period;

	if (symbol_conf.use_callchain)
		printed += fprintf(fp, "%c ", folded_sign);

	printed += fprintf(fp, " %5.2f%%", percent);

	if (symbol_conf.show_nr_samples)
		printed += fprintf(fp, " %11u", he->stat.nr_events);

	if (symbol_conf.show_total_period)
		printed += fprintf(fp, " %12" PRIu64, he->stat.period);

	printed += fprintf(fp, "%s\n", rtrim(s));

	if (folded_sign == '-')
		printed += hist_browser__fprintf_callchain(browser, &he->sorted_chain, 1, fp);

	return printed;
}

static int hist_browser__fprintf(struct hist_browser *browser, FILE *fp)
{
	struct rb_node *nd = hists__filter_entries(rb_first(browser->b.entries),
						   browser->hists,
						   browser->min_pcnt);
	int printed = 0;

	while (nd) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		printed += hist_browser__fprintf_entry(browser, h, fp);
		nd = hists__filter_entries(rb_next(nd), browser->hists,
					   browser->min_pcnt);
	}

	return printed;
}

static int hist_browser__dump(struct hist_browser *browser)
{
	char filename[64];
	FILE *fp;

	while (1) {
		scnprintf(filename, sizeof(filename), "perf.hist.%d", browser->print_seq);
		if (access(filename, F_OK))
			break;
		/*
 		 * XXX: Just an arbitrary lazy upper limit
 		 */
		if (++browser->print_seq == 8192) {
			ui_helpline__fpush("Too many perf.hist.N files, nothing written!");
			return -1;
		}
	}

	fp = fopen(filename, "w");
	if (fp == NULL) {
		char bf[64];
		const char *err = strerror_r(errno, bf, sizeof(bf));
		ui_helpline__fpush("Couldn't write to %s: %s", filename, err);
		return -1;
	}

	++browser->print_seq;
	hist_browser__fprintf(browser, fp);
	fclose(fp);
	ui_helpline__fpush("%s written!", filename);

	return 0;
}

static struct hist_browser *hist_browser__new(struct hists *hists)
{
	struct hist_browser *browser = zalloc(sizeof(*browser));

	if (browser) {
		browser->hists = hists;
		browser->b.refresh = hist_browser__refresh;
		browser->b.seek = ui_browser__hists_seek;
		browser->b.use_navkeypressed = true;
	}

	return browser;
}

static void hist_browser__delete(struct hist_browser *browser)
{
	free(browser);
}

static struct hist_entry *hist_browser__selected_entry(struct hist_browser *browser)
{
	return browser->he_selection;
}

static struct thread *hist_browser__selected_thread(struct hist_browser *browser)
{
	return browser->he_selection->thread;
}

static int hists__browser_title(struct hists *hists, char *bf, size_t size,
				const char *ev_name)
{
	char unit;
	int printed;
	const struct dso *dso = hists->dso_filter;
	const struct thread *thread = hists->thread_filter;
	unsigned long nr_samples = hists->stats.nr_events[PERF_RECORD_SAMPLE];
	u64 nr_events = hists->stats.total_period;
	struct perf_evsel *evsel = hists_to_evsel(hists);
	char buf[512];
	size_t buflen = sizeof(buf);

	if (perf_evsel__is_group_event(evsel)) {
		struct perf_evsel *pos;

		perf_evsel__group_desc(evsel, buf, buflen);
		ev_name = buf;

		for_each_group_member(pos, evsel) {
			nr_samples += pos->hists.stats.nr_events[PERF_RECORD_SAMPLE];
			nr_events += pos->hists.stats.total_period;
		}
	}

	nr_samples = convert_unit(nr_samples, &unit);
	printed = scnprintf(bf, size,
			   "Samples: %lu%c of event '%s', Event count (approx.): %lu",
			   nr_samples, unit, ev_name, nr_events);


	if (hists->uid_filter_str)
		printed += snprintf(bf + printed, size - printed,
				    ", UID: %s", hists->uid_filter_str);
	if (thread)
		printed += scnprintf(bf + printed, size - printed,
				    ", Thread: %s(%d)",
				     (thread->comm_set ? thread__comm_str(thread) : ""),
				    thread->tid);
	if (dso)
		printed += scnprintf(bf + printed, size - printed,
				    ", DSO: %s", dso->short_name);
	return printed;
}

static inline void free_popup_options(char **options, int n)
{
	int i;

	for (i = 0; i < n; ++i) {
		free(options[i]);
		options[i] = NULL;
	}
}

/* Check whether the browser is for 'top' or 'report' */
static inline bool is_report_browser(void *timer)
{
	return timer == NULL;
}

/*
 * Only runtime switching of perf data file will make "input_name" point
 * to a malloced buffer. So add "is_input_name_malloced" flag to decide
 * whether we need to call free() for current "input_name" during the switch.
 */
static bool is_input_name_malloced = false;

static int switch_data_file(void)
{
	char *pwd, *options[32], *abs_path[32], *tmp;
	DIR *pwd_dir;
	int nr_options = 0, choice = -1, ret = -1;
	struct dirent *dent;

	pwd = getenv("PWD");
	if (!pwd)
		return ret;

	pwd_dir = opendir(pwd);
	if (!pwd_dir)
		return ret;

	memset(options, 0, sizeof(options));
	memset(options, 0, sizeof(abs_path));

	while ((dent = readdir(pwd_dir))) {
		char path[PATH_MAX];
		u64 magic;
		char *name = dent->d_name;
		FILE *file;

		if (!(dent->d_type == DT_REG))
			continue;

		snprintf(path, sizeof(path), "%s/%s", pwd, name);

		file = fopen(path, "r");
		if (!file)
			continue;

		if (fread(&magic, 1, 8, file) < 8)
			goto close_file_and_continue;

		if (is_perf_magic(magic)) {
			options[nr_options] = strdup(name);
			if (!options[nr_options])
				goto close_file_and_continue;

			abs_path[nr_options] = strdup(path);
			if (!abs_path[nr_options]) {
				free(options[nr_options]);
				ui__warning("Can't search all data files due to memory shortage.\n");
				fclose(file);
				break;
			}

			nr_options++;
		}

close_file_and_continue:
		fclose(file);
		if (nr_options >= 32) {
			ui__warning("Too many perf data files in PWD!\n"
				    "Only the first 32 files will be listed.\n");
			break;
		}
	}
	closedir(pwd_dir);

	if (nr_options) {
		choice = ui__popup_menu(nr_options, options);
		if (choice < nr_options && choice >= 0) {
			tmp = strdup(abs_path[choice]);
			if (tmp) {
				if (is_input_name_malloced)
					free((void *)input_name);
				input_name = tmp;
				is_input_name_malloced = true;
				ret = 0;
			} else
				ui__warning("Data switch failed due to memory shortage!\n");
		}
	}

	free_popup_options(options, nr_options);
	free_popup_options(abs_path, nr_options);
	return ret;
}

static void hist_browser__update_pcnt_entries(struct hist_browser *hb)
{
	u64 nr_entries = 0;
	struct rb_node *nd = rb_first(&hb->hists->entries);

	while (nd) {
		nr_entries++;
		nd = hists__filter_entries(rb_next(nd), hb->hists,
					   hb->min_pcnt);
	}

	hb->nr_pcnt_entries = nr_entries;
}

static int perf_evsel__hists_browse(struct perf_evsel *evsel, int nr_events,
				    const char *helpline, const char *ev_name,
				    bool left_exits,
				    struct hist_browser_timer *hbt,
				    float min_pcnt,
				    struct perf_session_env *env)
{
	struct hists *hists = &evsel->hists;
	struct hist_browser *browser = hist_browser__new(hists);
	struct branch_info *bi;
	struct pstack *fstack;
	char *options[16];
	int nr_options = 0;
	int key = -1;
	char buf[64];
	char script_opt[64];
	int delay_secs = hbt ? hbt->refresh : 0;

	if (browser == NULL)
		return -1;

	if (min_pcnt) {
		browser->min_pcnt = min_pcnt;
		hist_browser__update_pcnt_entries(browser);
	}

	fstack = pstack__new(2);
	if (fstack == NULL)
		goto out;

	ui_helpline__push(helpline);

	memset(options, 0, sizeof(options));

	while (1) {
		const struct thread *thread = NULL;
		const struct dso *dso = NULL;
		int choice = 0,
		    annotate = -2, zoom_dso = -2, zoom_thread = -2,
		    annotate_f = -2, annotate_t = -2, browse_map = -2;
		int scripts_comm = -2, scripts_symbol = -2,
		    scripts_all = -2, switch_data = -2;

		nr_options = 0;

		key = hist_browser__run(browser, ev_name, hbt);

		if (browser->he_selection != NULL) {
			thread = hist_browser__selected_thread(browser);
			dso = browser->selection->map ? browser->selection->map->dso : NULL;
		}
		switch (key) {
		case K_TAB:
		case K_UNTAB:
			if (nr_events == 1)
				continue;
			/*
			 * Exit the browser, let hists__browser_tree
			 * go to the next or previous
			 */
			goto out_free_stack;
		case 'a':
			if (!sort__has_sym) {
				ui_browser__warning(&browser->b, delay_secs * 2,
			"Annotation is only available for symbolic views, "
			"include \"sym*\" in --sort to use it.");
				continue;
			}

			if (browser->selection == NULL ||
			    browser->selection->sym == NULL ||
			    browser->selection->map->dso->annotate_warned)
				continue;
			goto do_annotate;
		case 'P':
			hist_browser__dump(browser);
			continue;
		case 'd':
			goto zoom_dso;
		case 'V':
			browser->show_dso = !browser->show_dso;
			continue;
		case 't':
			goto zoom_thread;
		case '/':
			if (ui_browser__input_window("Symbol to show",
					"Please enter the name of symbol you want to see",
					buf, "ENTER: OK, ESC: Cancel",
					delay_secs * 2) == K_ENTER) {
				hists->symbol_filter_str = *buf ? buf : NULL;
				hists__filter_by_symbol(hists);
				hist_browser__reset(browser);
			}
			continue;
		case 'r':
			if (is_report_browser(hbt))
				goto do_scripts;
			continue;
		case 's':
			if (is_report_browser(hbt))
				goto do_data_switch;
			continue;
		case K_F1:
		case 'h':
		case '?':
			ui_browser__help_window(&browser->b,
					"h/?/F1        Show this window\n"
					"UP/DOWN/PGUP\n"
					"PGDN/SPACE    Navigate\n"
					"q/ESC/CTRL+C  Exit browser\n\n"
					"For multiple event sessions:\n\n"
					"TAB/UNTAB Switch events\n\n"
					"For symbolic views (--sort has sym):\n\n"
					"->            Zoom into DSO/Threads & Annotate current symbol\n"
					"<-            Zoom out\n"
					"a             Annotate current symbol\n"
					"C             Collapse all callchains\n"
					"E             Expand all callchains\n"
					"d             Zoom into current DSO\n"
					"t             Zoom into current Thread\n"
					"r             Run available scripts('perf report' only)\n"
					"s             Switch to another data file in PWD ('perf report' only)\n"
					"P             Print histograms to perf.hist.N\n"
					"V             Verbose (DSO names in callchains, etc)\n"
					"/             Filter symbol by name");
			continue;
		case K_ENTER:
		case K_RIGHT:
			/* menu */
			break;
		case K_LEFT: {
			const void *top;

			if (pstack__empty(fstack)) {
				/*
				 * Go back to the perf_evsel_menu__run or other user
				 */
				if (left_exits)
					goto out_free_stack;
				continue;
			}
			top = pstack__pop(fstack);
			if (top == &browser->hists->dso_filter)
				goto zoom_out_dso;
			if (top == &browser->hists->thread_filter)
				goto zoom_out_thread;
			continue;
		}
		case K_ESC:
			if (!left_exits &&
			    !ui_browser__dialog_yesno(&browser->b,
					       "Do you really want to exit?"))
				continue;
			/* Fall thru */
		case 'q':
		case CTRL('c'):
			goto out_free_stack;
		default:
			continue;
		}

		if (!sort__has_sym)
			goto add_exit_option;

		if (sort__mode == SORT_MODE__BRANCH) {
			bi = browser->he_selection->branch_info;
			if (browser->selection != NULL &&
			    bi &&
			    bi->from.sym != NULL &&
			    !bi->from.map->dso->annotate_warned &&
				asprintf(&options[nr_options], "Annotate %s",
					 bi->from.sym->name) > 0)
				annotate_f = nr_options++;

			if (browser->selection != NULL &&
			    bi &&
			    bi->to.sym != NULL &&
			    !bi->to.map->dso->annotate_warned &&
			    (bi->to.sym != bi->from.sym ||
			     bi->to.map->dso != bi->from.map->dso) &&
				asprintf(&options[nr_options], "Annotate %s",
					 bi->to.sym->name) > 0)
				annotate_t = nr_options++;
		} else {

			if (browser->selection != NULL &&
			    browser->selection->sym != NULL &&
			    !browser->selection->map->dso->annotate_warned &&
				asprintf(&options[nr_options], "Annotate %s",
					 browser->selection->sym->name) > 0)
				annotate = nr_options++;
		}

		if (thread != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s(%d) thread",
			     (browser->hists->thread_filter ? "out of" : "into"),
			     (thread->comm_set ? thread__comm_str(thread) : ""),
			     thread->tid) > 0)
			zoom_thread = nr_options++;

		if (dso != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s DSO",
			     (browser->hists->dso_filter ? "out of" : "into"),
			     (dso->kernel ? "the Kernel" : dso->short_name)) > 0)
			zoom_dso = nr_options++;

		if (browser->selection != NULL &&
		    browser->selection->map != NULL &&
		    asprintf(&options[nr_options], "Browse map details") > 0)
			browse_map = nr_options++;

		/* perf script support */
		if (browser->he_selection) {
			struct symbol *sym;

			if (asprintf(&options[nr_options], "Run scripts for samples of thread [%s]",
				     thread__comm_str(browser->he_selection->thread)) > 0)
				scripts_comm = nr_options++;

			sym = browser->he_selection->ms.sym;
			if (sym && sym->namelen &&
				asprintf(&options[nr_options], "Run scripts for samples of symbol [%s]",
						sym->name) > 0)
				scripts_symbol = nr_options++;
		}

		if (asprintf(&options[nr_options], "Run scripts for all samples") > 0)
			scripts_all = nr_options++;

		if (is_report_browser(hbt) && asprintf(&options[nr_options],
				"Switch to another data file in PWD") > 0)
			switch_data = nr_options++;
add_exit_option:
		options[nr_options++] = (char *)"Exit";
retry_popup_menu:
		choice = ui__popup_menu(nr_options, options);

		if (choice == nr_options - 1)
			break;

		if (choice == -1) {
			free_popup_options(options, nr_options - 1);
			continue;
		}

		if (choice == annotate || choice == annotate_t || choice == annotate_f) {
			struct hist_entry *he;
			int err;
do_annotate:
			if (!objdump_path && perf_session_env__lookup_objdump(env))
				continue;

			he = hist_browser__selected_entry(browser);
			if (he == NULL)
				continue;

			/*
			 * we stash the branch_info symbol + map into the
			 * the ms so we don't have to rewrite all the annotation
			 * code to use branch_info.
			 * in branch mode, the ms struct is not used
			 */
			if (choice == annotate_f) {
				he->ms.sym = he->branch_info->from.sym;
				he->ms.map = he->branch_info->from.map;
			}  else if (choice == annotate_t) {
				he->ms.sym = he->branch_info->to.sym;
				he->ms.map = he->branch_info->to.map;
			}

			/*
			 * Don't let this be freed, say, by hists__decay_entry.
			 */
			he->used = true;
			err = hist_entry__tui_annotate(he, evsel, hbt);
			he->used = false;
			/*
			 * offer option to annotate the other branch source or target
			 * (if they exists) when returning from annotate
			 */
			if ((err == 'q' || err == CTRL('c'))
			    && annotate_t != -2 && annotate_f != -2)
				goto retry_popup_menu;

			ui_browser__update_nr_entries(&browser->b, browser->hists->nr_entries);
			if (err)
				ui_browser__handle_resize(&browser->b);

		} else if (choice == browse_map)
			map__browse(browser->selection->map);
		else if (choice == zoom_dso) {
zoom_dso:
			if (browser->hists->dso_filter) {
				pstack__remove(fstack, &browser->hists->dso_filter);
zoom_out_dso:
				ui_helpline__pop();
				browser->hists->dso_filter = NULL;
				sort_dso.elide = false;
			} else {
				if (dso == NULL)
					continue;
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s DSO\"",
						   dso->kernel ? "the Kernel" : dso->short_name);
				browser->hists->dso_filter = dso;
				sort_dso.elide = true;
				pstack__push(fstack, &browser->hists->dso_filter);
			}
			hists__filter_by_dso(hists);
			hist_browser__reset(browser);
		} else if (choice == zoom_thread) {
zoom_thread:
			if (browser->hists->thread_filter) {
				pstack__remove(fstack, &browser->hists->thread_filter);
zoom_out_thread:
				ui_helpline__pop();
				browser->hists->thread_filter = NULL;
				sort_thread.elide = false;
			} else {
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s(%d) thread\"",
						   thread->comm_set ? thread__comm_str(thread) : "",
						   thread->tid);
				browser->hists->thread_filter = thread;
				sort_thread.elide = true;
				pstack__push(fstack, &browser->hists->thread_filter);
			}
			hists__filter_by_thread(hists);
			hist_browser__reset(browser);
		}
		/* perf scripts support */
		else if (choice == scripts_all || choice == scripts_comm ||
				choice == scripts_symbol) {
do_scripts:
			memset(script_opt, 0, 64);

			if (choice == scripts_comm)
				sprintf(script_opt, " -c %s ", thread__comm_str(browser->he_selection->thread));

			if (choice == scripts_symbol)
				sprintf(script_opt, " -S %s ", browser->he_selection->ms.sym->name);

			script_browse(script_opt);
		}
		/* Switch to another data file */
		else if (choice == switch_data) {
do_data_switch:
			if (!switch_data_file()) {
				key = K_SWITCH_INPUT_DATA;
				break;
			} else
				ui__warning("Won't switch the data files due to\n"
					"no valid data file get selected!\n");
		}
	}
out_free_stack:
	pstack__delete(fstack);
out:
	hist_browser__delete(browser);
	free_popup_options(options, nr_options - 1);
	return key;
}

struct perf_evsel_menu {
	struct ui_browser b;
	struct perf_evsel *selection;
	bool lost_events, lost_events_warned;
	float min_pcnt;
	struct perf_session_env *env;
};

static void perf_evsel_menu__write(struct ui_browser *browser,
				   void *entry, int row)
{
	struct perf_evsel_menu *menu = container_of(browser,
						    struct perf_evsel_menu, b);
	struct perf_evsel *evsel = list_entry(entry, struct perf_evsel, node);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	unsigned long nr_events = evsel->hists.stats.nr_events[PERF_RECORD_SAMPLE];
	const char *ev_name = perf_evsel__name(evsel);
	char bf[256], unit;
	const char *warn = " ";
	size_t printed;

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);

	if (perf_evsel__is_group_event(evsel)) {
		struct perf_evsel *pos;

		ev_name = perf_evsel__group_name(evsel);

		for_each_group_member(pos, evsel) {
			nr_events += pos->hists.stats.nr_events[PERF_RECORD_SAMPLE];
		}
	}

	nr_events = convert_unit(nr_events, &unit);
	printed = scnprintf(bf, sizeof(bf), "%lu%c%s%s", nr_events,
			   unit, unit == ' ' ? "" : " ", ev_name);
	slsmg_printf("%s", bf);

	nr_events = evsel->hists.stats.nr_events[PERF_RECORD_LOST];
	if (nr_events != 0) {
		menu->lost_events = true;
		if (!current_entry)
			ui_browser__set_color(browser, HE_COLORSET_TOP);
		nr_events = convert_unit(nr_events, &unit);
		printed += scnprintf(bf, sizeof(bf), ": %ld%c%schunks LOST!",
				     nr_events, unit, unit == ' ' ? "" : " ");
		warn = bf;
	}

	slsmg_write_nstring(warn, browser->width - printed);

	if (current_entry)
		menu->selection = evsel;
}

static int perf_evsel_menu__run(struct perf_evsel_menu *menu,
				int nr_events, const char *help,
				struct hist_browser_timer *hbt)
{
	struct perf_evlist *evlist = menu->b.priv;
	struct perf_evsel *pos;
	const char *ev_name, *title = "Available samples";
	int delay_secs = hbt ? hbt->refresh : 0;
	int key;

	if (ui_browser__show(&menu->b, title,
			     "ESC: exit, ENTER|->: Browse histograms") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&menu->b, delay_secs);

		switch (key) {
		case K_TIMER:
			hbt->timer(hbt->arg);

			if (!menu->lost_events_warned && menu->lost_events) {
				ui_browser__warn_lost_events(&menu->b);
				menu->lost_events_warned = true;
			}
			continue;
		case K_RIGHT:
		case K_ENTER:
			if (!menu->selection)
				continue;
			pos = menu->selection;
browse_hists:
			perf_evlist__set_selected(evlist, pos);
			/*
			 * Give the calling tool a chance to populate the non
			 * default evsel resorted hists tree.
			 */
			if (hbt)
				hbt->timer(hbt->arg);
			ev_name = perf_evsel__name(pos);
			key = perf_evsel__hists_browse(pos, nr_events, help,
						       ev_name, true, hbt,
						       menu->min_pcnt,
						       menu->env);
			ui_browser__show_title(&menu->b, title);
			switch (key) {
			case K_TAB:
				if (pos->node.next == &evlist->entries)
					pos = perf_evlist__first(evlist);
				else
					pos = perf_evsel__next(pos);
				goto browse_hists;
			case K_UNTAB:
				if (pos->node.prev == &evlist->entries)
					pos = perf_evlist__last(evlist);
				else
					pos = perf_evsel__prev(pos);
				goto browse_hists;
			case K_ESC:
				if (!ui_browser__dialog_yesno(&menu->b,
						"Do you really want to exit?"))
					continue;
				/* Fall thru */
			case K_SWITCH_INPUT_DATA:
			case 'q':
			case CTRL('c'):
				goto out;
			default:
				continue;
			}
		case K_LEFT:
			continue;
		case K_ESC:
			if (!ui_browser__dialog_yesno(&menu->b,
					       "Do you really want to exit?"))
				continue;
			/* Fall thru */
		case 'q':
		case CTRL('c'):
			goto out;
		default:
			continue;
		}
	}

out:
	ui_browser__hide(&menu->b);
	return key;
}

static bool filter_group_entries(struct ui_browser *browser __maybe_unused,
				 void *entry)
{
	struct perf_evsel *evsel = list_entry(entry, struct perf_evsel, node);

	if (symbol_conf.event_group && !perf_evsel__is_group_leader(evsel))
		return true;

	return false;
}

static int __perf_evlist__tui_browse_hists(struct perf_evlist *evlist,
					   int nr_entries, const char *help,
					   struct hist_browser_timer *hbt,
					   float min_pcnt,
					   struct perf_session_env *env)
{
	struct perf_evsel *pos;
	struct perf_evsel_menu menu = {
		.b = {
			.entries    = &evlist->entries,
			.refresh    = ui_browser__list_head_refresh,
			.seek	    = ui_browser__list_head_seek,
			.write	    = perf_evsel_menu__write,
			.filter	    = filter_group_entries,
			.nr_entries = nr_entries,
			.priv	    = evlist,
		},
		.min_pcnt = min_pcnt,
		.env = env,
	};

	ui_helpline__push("Press ESC to exit");

	list_for_each_entry(pos, &evlist->entries, node) {
		const char *ev_name = perf_evsel__name(pos);
		size_t line_len = strlen(ev_name) + 7;

		if (menu.b.width < line_len)
			menu.b.width = line_len;
	}

	return perf_evsel_menu__run(&menu, nr_entries, help, hbt);
}

int perf_evlist__tui_browse_hists(struct perf_evlist *evlist, const char *help,
				  struct hist_browser_timer *hbt,
				  float min_pcnt,
				  struct perf_session_env *env)
{
	int nr_entries = evlist->nr_entries;

single_entry:
	if (nr_entries == 1) {
		struct perf_evsel *first = perf_evlist__first(evlist);
		const char *ev_name = perf_evsel__name(first);

		return perf_evsel__hists_browse(first, nr_entries, help,
						ev_name, false, hbt, min_pcnt,
						env);
	}

	if (symbol_conf.event_group) {
		struct perf_evsel *pos;

		nr_entries = 0;
		list_for_each_entry(pos, &evlist->entries, node)
			if (perf_evsel__is_group_leader(pos))
				nr_entries++;

		if (nr_entries == 1)
			goto single_entry;
	}

	return __perf_evlist__tui_browse_hists(evlist, nr_entries, help,
					       hbt, min_pcnt, env);
}

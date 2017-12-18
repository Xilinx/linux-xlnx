#include <linux/hw_breakpoint.h>
#include <linux/err.h>
#include "util.h"
#include "../perf.h"
#include "evlist.h"
#include "evsel.h"
#include <subcmd/parse-options.h>
#include "parse-events.h"
#include <subcmd/exec-cmd.h>
#include "string.h"
#include "symbol.h"
#include "cache.h"
#include "header.h"
#include "bpf-loader.h"
#include "debug.h"
#include <api/fs/tracing_path.h>
#include "parse-events-bison.h"
#define YY_EXTRA_TYPE int
#include "parse-events-flex.h"
#include "pmu.h"
#include "thread_map.h"
#include "cpumap.h"
#include "probe-file.h"
#include "asm/bug.h"

#define MAX_NAME_LEN 100

#ifdef PARSER_DEBUG
extern int parse_events_debug;
#endif
int parse_events_parse(void *data, void *scanner);
static int get_config_terms(struct list_head *head_config,
			    struct list_head *head_terms __maybe_unused);

static struct perf_pmu_event_symbol *perf_pmu_events_list;
/*
 * The variable indicates the number of supported pmu event symbols.
 * 0 means not initialized and ready to init
 * -1 means failed to init, don't try anymore
 * >0 is the number of supported pmu event symbols
 */
static int perf_pmu_events_list_num;

struct event_symbol event_symbols_hw[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES] = {
		.symbol = "cpu-cycles",
		.alias  = "cycles",
	},
	[PERF_COUNT_HW_INSTRUCTIONS] = {
		.symbol = "instructions",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_REFERENCES] = {
		.symbol = "cache-references",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_MISSES] = {
		.symbol = "cache-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = {
		.symbol = "branch-instructions",
		.alias  = "branches",
	},
	[PERF_COUNT_HW_BRANCH_MISSES] = {
		.symbol = "branch-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BUS_CYCLES] = {
		.symbol = "bus-cycles",
		.alias  = "",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = {
		.symbol = "stalled-cycles-frontend",
		.alias  = "idle-cycles-frontend",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = {
		.symbol = "stalled-cycles-backend",
		.alias  = "idle-cycles-backend",
	},
	[PERF_COUNT_HW_REF_CPU_CYCLES] = {
		.symbol = "ref-cycles",
		.alias  = "",
	},
};

struct event_symbol event_symbols_sw[PERF_COUNT_SW_MAX] = {
	[PERF_COUNT_SW_CPU_CLOCK] = {
		.symbol = "cpu-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_TASK_CLOCK] = {
		.symbol = "task-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS] = {
		.symbol = "page-faults",
		.alias  = "faults",
	},
	[PERF_COUNT_SW_CONTEXT_SWITCHES] = {
		.symbol = "context-switches",
		.alias  = "cs",
	},
	[PERF_COUNT_SW_CPU_MIGRATIONS] = {
		.symbol = "cpu-migrations",
		.alias  = "migrations",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MIN] = {
		.symbol = "minor-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MAJ] = {
		.symbol = "major-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_ALIGNMENT_FAULTS] = {
		.symbol = "alignment-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_EMULATION_FAULTS] = {
		.symbol = "emulation-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_DUMMY] = {
		.symbol = "dummy",
		.alias  = "",
	},
	[PERF_COUNT_SW_BPF_OUTPUT] = {
		.symbol = "bpf-output",
		.alias  = "",
	},
};

#define __PERF_EVENT_FIELD(config, name) \
	((config & PERF_EVENT_##name##_MASK) >> PERF_EVENT_##name##_SHIFT)

#define PERF_EVENT_RAW(config)		__PERF_EVENT_FIELD(config, RAW)
#define PERF_EVENT_CONFIG(config)	__PERF_EVENT_FIELD(config, CONFIG)
#define PERF_EVENT_TYPE(config)		__PERF_EVENT_FIELD(config, TYPE)
#define PERF_EVENT_ID(config)		__PERF_EVENT_FIELD(config, EVENT)

#define for_each_subsystem(sys_dir, sys_dirent)			\
	while ((sys_dirent = readdir(sys_dir)) != NULL)		\
		if (sys_dirent->d_type == DT_DIR &&		\
		    (strcmp(sys_dirent->d_name, ".")) &&	\
		    (strcmp(sys_dirent->d_name, "..")))

static int tp_event_has_id(struct dirent *sys_dir, struct dirent *evt_dir)
{
	char evt_path[MAXPATHLEN];
	int fd;

	snprintf(evt_path, MAXPATHLEN, "%s/%s/%s/id", tracing_events_path,
			sys_dir->d_name, evt_dir->d_name);
	fd = open(evt_path, O_RDONLY);
	if (fd < 0)
		return -EINVAL;
	close(fd);

	return 0;
}

#define for_each_event(sys_dirent, evt_dir, evt_dirent)		\
	while ((evt_dirent = readdir(evt_dir)) != NULL)		\
		if (evt_dirent->d_type == DT_DIR &&		\
		    (strcmp(evt_dirent->d_name, ".")) &&	\
		    (strcmp(evt_dirent->d_name, "..")) &&	\
		    (!tp_event_has_id(sys_dirent, evt_dirent)))

#define MAX_EVENT_LENGTH 512


struct tracepoint_path *tracepoint_id_to_path(u64 config)
{
	struct tracepoint_path *path = NULL;
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_dirent, *evt_dirent;
	char id_buf[24];
	int fd;
	u64 id;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];

	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return NULL;

	for_each_subsystem(sys_dir, sys_dirent) {

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent->d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent) {

			snprintf(evt_path, MAXPATHLEN, "%s/%s/id", dir_path,
				 evt_dirent->d_name);
			fd = open(evt_path, O_RDONLY);
			if (fd < 0)
				continue;
			if (read(fd, id_buf, sizeof(id_buf)) < 0) {
				close(fd);
				continue;
			}
			close(fd);
			id = atoll(id_buf);
			if (id == config) {
				closedir(evt_dir);
				closedir(sys_dir);
				path = zalloc(sizeof(*path));
				path->system = malloc(MAX_EVENT_LENGTH);
				if (!path->system) {
					free(path);
					return NULL;
				}
				path->name = malloc(MAX_EVENT_LENGTH);
				if (!path->name) {
					zfree(&path->system);
					free(path);
					return NULL;
				}
				strncpy(path->system, sys_dirent->d_name,
					MAX_EVENT_LENGTH);
				strncpy(path->name, evt_dirent->d_name,
					MAX_EVENT_LENGTH);
				return path;
			}
		}
		closedir(evt_dir);
	}

	closedir(sys_dir);
	return NULL;
}

struct tracepoint_path *tracepoint_name_to_path(const char *name)
{
	struct tracepoint_path *path = zalloc(sizeof(*path));
	char *str = strchr(name, ':');

	if (path == NULL || str == NULL) {
		free(path);
		return NULL;
	}

	path->system = strndup(name, str - name);
	path->name = strdup(str+1);

	if (path->system == NULL || path->name == NULL) {
		zfree(&path->system);
		zfree(&path->name);
		free(path);
		path = NULL;
	}

	return path;
}

const char *event_type(int type)
{
	switch (type) {
	case PERF_TYPE_HARDWARE:
		return "hardware";

	case PERF_TYPE_SOFTWARE:
		return "software";

	case PERF_TYPE_TRACEPOINT:
		return "tracepoint";

	case PERF_TYPE_HW_CACHE:
		return "hardware-cache";

	default:
		break;
	}

	return "unknown";
}

static int parse_events__is_name_term(struct parse_events_term *term)
{
	return term->type_term == PARSE_EVENTS__TERM_TYPE_NAME;
}

static char *get_config_name(struct list_head *head_terms)
{
	struct parse_events_term *term;

	if (!head_terms)
		return NULL;

	list_for_each_entry(term, head_terms, list)
		if (parse_events__is_name_term(term))
			return term->val.str;

	return NULL;
}

static struct perf_evsel *
__add_event(struct list_head *list, int *idx,
	    struct perf_event_attr *attr,
	    char *name, struct cpu_map *cpus,
	    struct list_head *config_terms)
{
	struct perf_evsel *evsel;

	event_attr_init(attr);

	evsel = perf_evsel__new_idx(attr, (*idx)++);
	if (!evsel)
		return NULL;

	evsel->cpus     = cpu_map__get(cpus);
	evsel->own_cpus = cpu_map__get(cpus);

	if (name)
		evsel->name = strdup(name);

	if (config_terms)
		list_splice(config_terms, &evsel->config_terms);

	list_add_tail(&evsel->node, list);
	return evsel;
}

static int add_event(struct list_head *list, int *idx,
		     struct perf_event_attr *attr, char *name,
		     struct list_head *config_terms)
{
	return __add_event(list, idx, attr, name, NULL, config_terms) ? 0 : -ENOMEM;
}

static int parse_aliases(char *str, const char *names[][PERF_EVSEL__MAX_ALIASES], int size)
{
	int i, j;
	int n, longest = -1;

	for (i = 0; i < size; i++) {
		for (j = 0; j < PERF_EVSEL__MAX_ALIASES && names[i][j]; j++) {
			n = strlen(names[i][j]);
			if (n > longest && !strncasecmp(str, names[i][j], n))
				longest = n;
		}
		if (longest > 0)
			return i;
	}

	return -1;
}

typedef int config_term_func_t(struct perf_event_attr *attr,
			       struct parse_events_term *term,
			       struct parse_events_error *err);
static int config_term_common(struct perf_event_attr *attr,
			      struct parse_events_term *term,
			      struct parse_events_error *err);
static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head,
		       struct parse_events_error *err,
		       config_term_func_t config_term);

int parse_events_add_cache(struct list_head *list, int *idx,
			   char *type, char *op_result1, char *op_result2,
			   struct parse_events_error *err,
			   struct list_head *head_config)
{
	struct perf_event_attr attr;
	LIST_HEAD(config_terms);
	char name[MAX_NAME_LEN], *config_name;
	int cache_type = -1, cache_op = -1, cache_result = -1;
	char *op_result[2] = { op_result1, op_result2 };
	int i, n;

	/*
	 * No fallback - if we cannot get a clear cache type
	 * then bail out:
	 */
	cache_type = parse_aliases(type, perf_evsel__hw_cache,
				   PERF_COUNT_HW_CACHE_MAX);
	if (cache_type == -1)
		return -EINVAL;

	config_name = get_config_name(head_config);
	n = snprintf(name, MAX_NAME_LEN, "%s", type);

	for (i = 0; (i < 2) && (op_result[i]); i++) {
		char *str = op_result[i];

		n += snprintf(name + n, MAX_NAME_LEN - n, "-%s", str);

		if (cache_op == -1) {
			cache_op = parse_aliases(str, perf_evsel__hw_cache_op,
						 PERF_COUNT_HW_CACHE_OP_MAX);
			if (cache_op >= 0) {
				if (!perf_evsel__is_cache_op_valid(cache_type, cache_op))
					return -EINVAL;
				continue;
			}
		}

		if (cache_result == -1) {
			cache_result = parse_aliases(str, perf_evsel__hw_cache_result,
						     PERF_COUNT_HW_CACHE_RESULT_MAX);
			if (cache_result >= 0)
				continue;
		}
	}

	/*
	 * Fall back to reads:
	 */
	if (cache_op == -1)
		cache_op = PERF_COUNT_HW_CACHE_OP_READ;

	/*
	 * Fall back to accesses:
	 */
	if (cache_result == -1)
		cache_result = PERF_COUNT_HW_CACHE_RESULT_ACCESS;

	memset(&attr, 0, sizeof(attr));
	attr.config = cache_type | (cache_op << 8) | (cache_result << 16);
	attr.type = PERF_TYPE_HW_CACHE;

	if (head_config) {
		if (config_attr(&attr, head_config, err,
				config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}
	return add_event(list, idx, &attr, config_name ? : name, &config_terms);
}

static void tracepoint_error(struct parse_events_error *e, int err,
			     const char *sys, const char *name)
{
	char help[BUFSIZ];

	if (!e)
		return;

	/*
	 * We get error directly from syscall errno ( > 0),
	 * or from encoded pointer's error ( < 0).
	 */
	err = abs(err);

	switch (err) {
	case EACCES:
		e->str = strdup("can't access trace events");
		break;
	case ENOENT:
		e->str = strdup("unknown tracepoint");
		break;
	default:
		e->str = strdup("failed to add tracepoint");
		break;
	}

	tracing_path__strerror_open_tp(err, help, sizeof(help), sys, name);
	e->help = strdup(help);
}

static int add_tracepoint(struct list_head *list, int *idx,
			  const char *sys_name, const char *evt_name,
			  struct parse_events_error *err,
			  struct list_head *head_config)
{
	struct perf_evsel *evsel;

	evsel = perf_evsel__newtp_idx(sys_name, evt_name, (*idx)++);
	if (IS_ERR(evsel)) {
		tracepoint_error(err, PTR_ERR(evsel), sys_name, evt_name);
		return PTR_ERR(evsel);
	}

	if (head_config) {
		LIST_HEAD(config_terms);

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
		list_splice(&config_terms, &evsel->config_terms);
	}

	list_add_tail(&evsel->node, list);
	return 0;
}

static int add_tracepoint_multi_event(struct list_head *list, int *idx,
				      const char *sys_name, const char *evt_name,
				      struct parse_events_error *err,
				      struct list_head *head_config)
{
	char evt_path[MAXPATHLEN];
	struct dirent *evt_ent;
	DIR *evt_dir;
	int ret = 0, found = 0;

	snprintf(evt_path, MAXPATHLEN, "%s/%s", tracing_events_path, sys_name);
	evt_dir = opendir(evt_path);
	if (!evt_dir) {
		tracepoint_error(err, errno, sys_name, evt_name);
		return -1;
	}

	while (!ret && (evt_ent = readdir(evt_dir))) {
		if (!strcmp(evt_ent->d_name, ".")
		    || !strcmp(evt_ent->d_name, "..")
		    || !strcmp(evt_ent->d_name, "enable")
		    || !strcmp(evt_ent->d_name, "filter"))
			continue;

		if (!strglobmatch(evt_ent->d_name, evt_name))
			continue;

		found++;

		ret = add_tracepoint(list, idx, sys_name, evt_ent->d_name,
				     err, head_config);
	}

	if (!found) {
		tracepoint_error(err, ENOENT, sys_name, evt_name);
		ret = -1;
	}

	closedir(evt_dir);
	return ret;
}

static int add_tracepoint_event(struct list_head *list, int *idx,
				const char *sys_name, const char *evt_name,
				struct parse_events_error *err,
				struct list_head *head_config)
{
	return strpbrk(evt_name, "*?") ?
	       add_tracepoint_multi_event(list, idx, sys_name, evt_name,
					  err, head_config) :
	       add_tracepoint(list, idx, sys_name, evt_name,
			      err, head_config);
}

static int add_tracepoint_multi_sys(struct list_head *list, int *idx,
				    const char *sys_name, const char *evt_name,
				    struct parse_events_error *err,
				    struct list_head *head_config)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int ret = 0;

	events_dir = opendir(tracing_events_path);
	if (!events_dir) {
		tracepoint_error(err, errno, sys_name, evt_name);
		return -1;
	}

	while (!ret && (events_ent = readdir(events_dir))) {
		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		if (!strglobmatch(events_ent->d_name, sys_name))
			continue;

		ret = add_tracepoint_event(list, idx, events_ent->d_name,
					   evt_name, err, head_config);
	}

	closedir(events_dir);
	return ret;
}

struct __add_bpf_event_param {
	struct parse_events_evlist *data;
	struct list_head *list;
	struct list_head *head_config;
};

static int add_bpf_event(const char *group, const char *event, int fd,
			 void *_param)
{
	LIST_HEAD(new_evsels);
	struct __add_bpf_event_param *param = _param;
	struct parse_events_evlist *evlist = param->data;
	struct list_head *list = param->list;
	struct perf_evsel *pos;
	int err;

	pr_debug("add bpf event %s:%s and attach bpf program %d\n",
		 group, event, fd);

	err = parse_events_add_tracepoint(&new_evsels, &evlist->idx, group,
					  event, evlist->error,
					  param->head_config);
	if (err) {
		struct perf_evsel *evsel, *tmp;

		pr_debug("Failed to add BPF event %s:%s\n",
			 group, event);
		list_for_each_entry_safe(evsel, tmp, &new_evsels, node) {
			list_del(&evsel->node);
			perf_evsel__delete(evsel);
		}
		return err;
	}
	pr_debug("adding %s:%s\n", group, event);

	list_for_each_entry(pos, &new_evsels, node) {
		pr_debug("adding %s:%s to %p\n",
			 group, event, pos);
		pos->bpf_fd = fd;
	}
	list_splice(&new_evsels, list);
	return 0;
}

int parse_events_load_bpf_obj(struct parse_events_evlist *data,
			      struct list_head *list,
			      struct bpf_object *obj,
			      struct list_head *head_config)
{
	int err;
	char errbuf[BUFSIZ];
	struct __add_bpf_event_param param = {data, list, head_config};
	static bool registered_unprobe_atexit = false;

	if (IS_ERR(obj) || !obj) {
		snprintf(errbuf, sizeof(errbuf),
			 "Internal error: load bpf obj with NULL");
		err = -EINVAL;
		goto errout;
	}

	/*
	 * Register atexit handler before calling bpf__probe() so
	 * bpf__probe() don't need to unprobe probe points its already
	 * created when failure.
	 */
	if (!registered_unprobe_atexit) {
		atexit(bpf__clear);
		registered_unprobe_atexit = true;
	}

	err = bpf__probe(obj);
	if (err) {
		bpf__strerror_probe(obj, err, errbuf, sizeof(errbuf));
		goto errout;
	}

	err = bpf__load(obj);
	if (err) {
		bpf__strerror_load(obj, err, errbuf, sizeof(errbuf));
		goto errout;
	}

	err = bpf__foreach_event(obj, add_bpf_event, &param);
	if (err) {
		snprintf(errbuf, sizeof(errbuf),
			 "Attach events in BPF object failed");
		goto errout;
	}

	return 0;
errout:
	data->error->help = strdup("(add -v to see detail)");
	data->error->str = strdup(errbuf);
	return err;
}

static int
parse_events_config_bpf(struct parse_events_evlist *data,
			struct bpf_object *obj,
			struct list_head *head_config)
{
	struct parse_events_term *term;
	int error_pos;

	if (!head_config || list_empty(head_config))
		return 0;

	list_for_each_entry(term, head_config, list) {
		char errbuf[BUFSIZ];
		int err;

		if (term->type_term != PARSE_EVENTS__TERM_TYPE_USER) {
			snprintf(errbuf, sizeof(errbuf),
				 "Invalid config term for BPF object");
			errbuf[BUFSIZ - 1] = '\0';

			data->error->idx = term->err_term;
			data->error->str = strdup(errbuf);
			return -EINVAL;
		}

		err = bpf__config_obj(obj, term, data->evlist, &error_pos);
		if (err) {
			bpf__strerror_config_obj(obj, term, data->evlist,
						 &error_pos, err, errbuf,
						 sizeof(errbuf));
			data->error->help = strdup(
"Hint:\tValid config terms:\n"
"     \tmap:[<arraymap>].value<indices>=[value]\n"
"     \tmap:[<eventmap>].event<indices>=[event]\n"
"\n"
"     \twhere <indices> is something like [0,3...5] or [all]\n"
"     \t(add -v to see detail)");
			data->error->str = strdup(errbuf);
			if (err == -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUE)
				data->error->idx = term->err_val;
			else
				data->error->idx = term->err_term + error_pos;
			return err;
		}
	}
	return 0;
}

/*
 * Split config terms:
 * perf record -e bpf.c/call-graph=fp,map:array.value[0]=1/ ...
 *  'call-graph=fp' is 'evt config', should be applied to each
 *  events in bpf.c.
 * 'map:array.value[0]=1' is 'obj config', should be processed
 * with parse_events_config_bpf.
 *
 * Move object config terms from the first list to obj_head_config.
 */
static void
split_bpf_config_terms(struct list_head *evt_head_config,
		       struct list_head *obj_head_config)
{
	struct parse_events_term *term, *temp;

	/*
	 * Currectly, all possible user config term
	 * belong to bpf object. parse_events__is_hardcoded_term()
	 * happends to be a good flag.
	 *
	 * See parse_events_config_bpf() and
	 * config_term_tracepoint().
	 */
	list_for_each_entry_safe(term, temp, evt_head_config, list)
		if (!parse_events__is_hardcoded_term(term))
			list_move_tail(&term->list, obj_head_config);
}

int parse_events_load_bpf(struct parse_events_evlist *data,
			  struct list_head *list,
			  char *bpf_file_name,
			  bool source,
			  struct list_head *head_config)
{
	int err;
	struct bpf_object *obj;
	LIST_HEAD(obj_head_config);

	if (head_config)
		split_bpf_config_terms(head_config, &obj_head_config);

	obj = bpf__prepare_load(bpf_file_name, source);
	if (IS_ERR(obj)) {
		char errbuf[BUFSIZ];

		err = PTR_ERR(obj);

		if (err == -ENOTSUP)
			snprintf(errbuf, sizeof(errbuf),
				 "BPF support is not compiled");
		else
			bpf__strerror_prepare_load(bpf_file_name,
						   source,
						   -err, errbuf,
						   sizeof(errbuf));

		data->error->help = strdup("(add -v to see detail)");
		data->error->str = strdup(errbuf);
		return err;
	}

	err = parse_events_load_bpf_obj(data, list, obj, head_config);
	if (err)
		return err;
	err = parse_events_config_bpf(data, obj, &obj_head_config);

	/*
	 * Caller doesn't know anything about obj_head_config,
	 * so combine them together again before returnning.
	 */
	if (head_config)
		list_splice_tail(&obj_head_config, head_config);
	return err;
}

static int
parse_breakpoint_type(const char *type, struct perf_event_attr *attr)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!type || !type[i])
			break;

#define CHECK_SET_TYPE(bit)		\
do {					\
	if (attr->bp_type & bit)	\
		return -EINVAL;		\
	else				\
		attr->bp_type |= bit;	\
} while (0)

		switch (type[i]) {
		case 'r':
			CHECK_SET_TYPE(HW_BREAKPOINT_R);
			break;
		case 'w':
			CHECK_SET_TYPE(HW_BREAKPOINT_W);
			break;
		case 'x':
			CHECK_SET_TYPE(HW_BREAKPOINT_X);
			break;
		default:
			return -EINVAL;
		}
	}

#undef CHECK_SET_TYPE

	if (!attr->bp_type) /* Default */
		attr->bp_type = HW_BREAKPOINT_R | HW_BREAKPOINT_W;

	return 0;
}

int parse_events_add_breakpoint(struct list_head *list, int *idx,
				void *ptr, char *type, u64 len)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.bp_addr = (unsigned long) ptr;

	if (parse_breakpoint_type(type, &attr))
		return -EINVAL;

	/* Provide some defaults if len is not specified */
	if (!len) {
		if (attr.bp_type == HW_BREAKPOINT_X)
			len = sizeof(long);
		else
			len = HW_BREAKPOINT_LEN_4;
	}

	attr.bp_len = len;

	attr.type = PERF_TYPE_BREAKPOINT;
	attr.sample_period = 1;

	return add_event(list, idx, &attr, NULL, NULL);
}

static int check_type_val(struct parse_events_term *term,
			  struct parse_events_error *err,
			  int type)
{
	if (type == term->type_val)
		return 0;

	if (err) {
		err->idx = term->err_val;
		if (type == PARSE_EVENTS__TERM_TYPE_NUM)
			err->str = strdup("expected numeric value");
		else
			err->str = strdup("expected string value");
	}
	return -EINVAL;
}

/*
 * Update according to parse-events.l
 */
static const char *config_term_names[__PARSE_EVENTS__TERM_TYPE_NR] = {
	[PARSE_EVENTS__TERM_TYPE_USER]			= "<sysfs term>",
	[PARSE_EVENTS__TERM_TYPE_CONFIG]		= "config",
	[PARSE_EVENTS__TERM_TYPE_CONFIG1]		= "config1",
	[PARSE_EVENTS__TERM_TYPE_CONFIG2]		= "config2",
	[PARSE_EVENTS__TERM_TYPE_NAME]			= "name",
	[PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD]		= "period",
	[PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ]		= "freq",
	[PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE]	= "branch_type",
	[PARSE_EVENTS__TERM_TYPE_TIME]			= "time",
	[PARSE_EVENTS__TERM_TYPE_CALLGRAPH]		= "call-graph",
	[PARSE_EVENTS__TERM_TYPE_STACKSIZE]		= "stack-size",
	[PARSE_EVENTS__TERM_TYPE_NOINHERIT]		= "no-inherit",
	[PARSE_EVENTS__TERM_TYPE_INHERIT]		= "inherit",
	[PARSE_EVENTS__TERM_TYPE_MAX_STACK]		= "max-stack",
	[PARSE_EVENTS__TERM_TYPE_OVERWRITE]		= "overwrite",
	[PARSE_EVENTS__TERM_TYPE_NOOVERWRITE]		= "no-overwrite",
	[PARSE_EVENTS__TERM_TYPE_DRV_CFG]		= "driver-config",
};

static bool config_term_shrinked;

static bool
config_term_avail(int term_type, struct parse_events_error *err)
{
	if (term_type < 0 || term_type >= __PARSE_EVENTS__TERM_TYPE_NR) {
		err->str = strdup("Invalid term_type");
		return false;
	}
	if (!config_term_shrinked)
		return true;

	switch (term_type) {
	case PARSE_EVENTS__TERM_TYPE_CONFIG:
	case PARSE_EVENTS__TERM_TYPE_CONFIG1:
	case PARSE_EVENTS__TERM_TYPE_CONFIG2:
	case PARSE_EVENTS__TERM_TYPE_NAME:
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
		return true;
	default:
		if (!err)
			return false;

		/* term_type is validated so indexing is safe */
		if (asprintf(&err->str, "'%s' is not usable in 'perf stat'",
			     config_term_names[term_type]) < 0)
			err->str = NULL;
		return false;
	}
}

void parse_events__shrink_config_terms(void)
{
	config_term_shrinked = true;
}

static int config_term_common(struct perf_event_attr *attr,
			      struct parse_events_term *term,
			      struct parse_events_error *err)
{
#define CHECK_TYPE_VAL(type)						   \
do {									   \
	if (check_type_val(term, err, PARSE_EVENTS__TERM_TYPE_ ## type)) \
		return -EINVAL;						   \
} while (0)

	switch (term->type_term) {
	case PARSE_EVENTS__TERM_TYPE_CONFIG:
		CHECK_TYPE_VAL(NUM);
		attr->config = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_CONFIG1:
		CHECK_TYPE_VAL(NUM);
		attr->config1 = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_CONFIG2:
		CHECK_TYPE_VAL(NUM);
		attr->config2 = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
		/*
		 * TODO uncomment when the field is available
		 * attr->branch_sample_type = term->val.num;
		 */
		break;
	case PARSE_EVENTS__TERM_TYPE_TIME:
		CHECK_TYPE_VAL(NUM);
		if (term->val.num > 1) {
			err->str = strdup("expected 0 or 1");
			err->idx = term->err_val;
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_INHERIT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NAME:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
		CHECK_TYPE_VAL(NUM);
		break;
	default:
		err->str = strdup("unknown term");
		err->idx = term->err_term;
		err->help = parse_events_formats_error_string(NULL);
		return -EINVAL;
	}

	/*
	 * Check term availbility after basic checking so
	 * PARSE_EVENTS__TERM_TYPE_USER can be found and filtered.
	 *
	 * If check availbility at the entry of this function,
	 * user will see "'<sysfs term>' is not usable in 'perf stat'"
	 * if an invalid config term is provided for legacy events
	 * (for example, instructions/badterm/...), which is confusing.
	 */
	if (!config_term_avail(term->type_term, err))
		return -EINVAL;
	return 0;
#undef CHECK_TYPE_VAL
}

static int config_term_pmu(struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct parse_events_error *err)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER ||
	    term->type_term == PARSE_EVENTS__TERM_TYPE_DRV_CFG)
		/*
		 * Always succeed for sysfs terms, as we dont know
		 * at this point what type they need to have.
		 */
		return 0;
	else
		return config_term_common(attr, term, err);
}

static int config_term_tracepoint(struct perf_event_attr *attr,
				  struct parse_events_term *term,
				  struct parse_events_error *err)
{
	switch (term->type_term) {
	case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
	case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
	case PARSE_EVENTS__TERM_TYPE_INHERIT:
	case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
	case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
	case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
	case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
		return config_term_common(attr, term, err);
	default:
		if (err) {
			err->idx = term->err_term;
			err->str = strdup("unknown term");
			err->help = strdup("valid terms: call-graph,stack-size\n");
		}
		return -EINVAL;
	}

	return 0;
}

static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head,
		       struct parse_events_error *err,
		       config_term_func_t config_term)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head, list)
		if (config_term(attr, term, err))
			return -EINVAL;

	return 0;
}

static int get_config_terms(struct list_head *head_config,
			    struct list_head *head_terms __maybe_unused)
{
#define ADD_CONFIG_TERM(__type, __name, __val)			\
do {								\
	struct perf_evsel_config_term *__t;			\
								\
	__t = zalloc(sizeof(*__t));				\
	if (!__t)						\
		return -ENOMEM;					\
								\
	INIT_LIST_HEAD(&__t->list);				\
	__t->type       = PERF_EVSEL__CONFIG_TERM_ ## __type;	\
	__t->val.__name = __val;				\
	list_add_tail(&__t->list, head_terms);			\
} while (0)

	struct parse_events_term *term;

	list_for_each_entry(term, head_config, list) {
		switch (term->type_term) {
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
			ADD_CONFIG_TERM(PERIOD, period, term->val.num);
			break;
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
			ADD_CONFIG_TERM(FREQ, freq, term->val.num);
			break;
		case PARSE_EVENTS__TERM_TYPE_TIME:
			ADD_CONFIG_TERM(TIME, time, term->val.num);
			break;
		case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
			ADD_CONFIG_TERM(CALLGRAPH, callgraph, term->val.str);
			break;
		case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
			ADD_CONFIG_TERM(STACK_USER, stack_user, term->val.num);
			break;
		case PARSE_EVENTS__TERM_TYPE_INHERIT:
			ADD_CONFIG_TERM(INHERIT, inherit, term->val.num ? 1 : 0);
			break;
		case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
			ADD_CONFIG_TERM(INHERIT, inherit, term->val.num ? 0 : 1);
			break;
		case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
			ADD_CONFIG_TERM(MAX_STACK, max_stack, term->val.num);
			break;
		case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
			ADD_CONFIG_TERM(OVERWRITE, overwrite, term->val.num ? 1 : 0);
			break;
		case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
			ADD_CONFIG_TERM(OVERWRITE, overwrite, term->val.num ? 0 : 1);
			break;
		case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
			ADD_CONFIG_TERM(DRV_CFG, drv_cfg, term->val.str);
			break;
		default:
			break;
		}
	}
#undef ADD_EVSEL_CONFIG
	return 0;
}

int parse_events_add_tracepoint(struct list_head *list, int *idx,
				const char *sys, const char *event,
				struct parse_events_error *err,
				struct list_head *head_config)
{
	if (head_config) {
		struct perf_event_attr attr;

		if (config_attr(&attr, head_config, err,
				config_term_tracepoint))
			return -EINVAL;
	}

	if (strpbrk(sys, "*?"))
		return add_tracepoint_multi_sys(list, idx, sys, event,
						err, head_config);
	else
		return add_tracepoint_event(list, idx, sys, event,
					    err, head_config);
}

int parse_events_add_numeric(struct parse_events_evlist *data,
			     struct list_head *list,
			     u32 type, u64 config,
			     struct list_head *head_config)
{
	struct perf_event_attr attr;
	LIST_HEAD(config_terms);

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.config = config;

	if (head_config) {
		if (config_attr(&attr, head_config, data->error,
				config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}

	return add_event(list, &data->idx, &attr,
			 get_config_name(head_config), &config_terms);
}

int parse_events_add_pmu(struct parse_events_evlist *data,
			 struct list_head *list, char *name,
			 struct list_head *head_config)
{
	struct perf_event_attr attr;
	struct perf_pmu_info info;
	struct perf_pmu *pmu;
	struct perf_evsel *evsel;
	LIST_HEAD(config_terms);

	pmu = perf_pmu__find(name);
	if (!pmu)
		return -EINVAL;

	if (pmu->default_config) {
		memcpy(&attr, pmu->default_config,
		       sizeof(struct perf_event_attr));
	} else {
		memset(&attr, 0, sizeof(attr));
	}

	if (!head_config) {
		attr.type = pmu->type;
		evsel = __add_event(list, &data->idx, &attr, NULL, pmu->cpus, NULL);
		return evsel ? 0 : -ENOMEM;
	}

	if (perf_pmu__check_alias(pmu, head_config, &info))
		return -EINVAL;

	/*
	 * Configure hardcoded terms first, no need to check
	 * return value when called with fail == 0 ;)
	 */
	if (config_attr(&attr, head_config, data->error, config_term_pmu))
		return -EINVAL;

	if (get_config_terms(head_config, &config_terms))
		return -ENOMEM;

	if (perf_pmu__config(pmu, &attr, head_config, data->error))
		return -EINVAL;

	evsel = __add_event(list, &data->idx, &attr,
			    get_config_name(head_config), pmu->cpus,
			    &config_terms);
	if (evsel) {
		evsel->unit = info.unit;
		evsel->scale = info.scale;
		evsel->per_pkg = info.per_pkg;
		evsel->snapshot = info.snapshot;
	}

	return evsel ? 0 : -ENOMEM;
}

int parse_events__modifier_group(struct list_head *list,
				 char *event_mod)
{
	return parse_events__modifier_event(list, event_mod, true);
}

void parse_events__set_leader(char *name, struct list_head *list)
{
	struct perf_evsel *leader;

	if (list_empty(list)) {
		WARN_ONCE(true, "WARNING: failed to set leader: empty list");
		return;
	}

	__perf_evlist__set_leader(list);
	leader = list_entry(list->next, struct perf_evsel, node);
	leader->group_name = name ? strdup(name) : NULL;
}

/* list_event is assumed to point to malloc'ed memory */
void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all)
{
	/*
	 * Called for single event definition. Update the
	 * 'all event' list, and reinit the 'single event'
	 * list, for next event definition.
	 */
	list_splice_tail(list_event, list_all);
	free(list_event);
}

struct event_modifier {
	int eu;
	int ek;
	int eh;
	int eH;
	int eG;
	int eI;
	int precise;
	int precise_max;
	int exclude_GH;
	int sample_read;
	int pinned;
};

static int get_event_modifier(struct event_modifier *mod, char *str,
			       struct perf_evsel *evsel)
{
	int eu = evsel ? evsel->attr.exclude_user : 0;
	int ek = evsel ? evsel->attr.exclude_kernel : 0;
	int eh = evsel ? evsel->attr.exclude_hv : 0;
	int eH = evsel ? evsel->attr.exclude_host : 0;
	int eG = evsel ? evsel->attr.exclude_guest : 0;
	int eI = evsel ? evsel->attr.exclude_idle : 0;
	int precise = evsel ? evsel->attr.precise_ip : 0;
	int precise_max = 0;
	int sample_read = 0;
	int pinned = evsel ? evsel->attr.pinned : 0;

	int exclude = eu | ek | eh;
	int exclude_GH = evsel ? evsel->exclude_GH : 0;

	memset(mod, 0, sizeof(*mod));

	while (*str) {
		if (*str == 'u') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eu = 0;
		} else if (*str == 'k') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			ek = 0;
		} else if (*str == 'h') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eh = 0;
		} else if (*str == 'G') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eG = 0;
		} else if (*str == 'H') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eH = 0;
		} else if (*str == 'I') {
			eI = 1;
		} else if (*str == 'p') {
			precise++;
			/* use of precise requires exclude_guest */
			if (!exclude_GH)
				eG = 1;
		} else if (*str == 'P') {
			precise_max = 1;
		} else if (*str == 'S') {
			sample_read = 1;
		} else if (*str == 'D') {
			pinned = 1;
		} else
			break;

		++str;
	}

	/*
	 * precise ip:
	 *
	 *  0 - SAMPLE_IP can have arbitrary skid
	 *  1 - SAMPLE_IP must have constant skid
	 *  2 - SAMPLE_IP requested to have 0 skid
	 *  3 - SAMPLE_IP must have 0 skid
	 *
	 *  See also PERF_RECORD_MISC_EXACT_IP
	 */
	if (precise > 3)
		return -EINVAL;

	mod->eu = eu;
	mod->ek = ek;
	mod->eh = eh;
	mod->eH = eH;
	mod->eG = eG;
	mod->eI = eI;
	mod->precise = precise;
	mod->precise_max = precise_max;
	mod->exclude_GH = exclude_GH;
	mod->sample_read = sample_read;
	mod->pinned = pinned;

	return 0;
}

/*
 * Basic modifier sanity check to validate it contains only one
 * instance of any modifier (apart from 'p') present.
 */
static int check_modifier(char *str)
{
	char *p = str;

	/* The sizeof includes 0 byte as well. */
	if (strlen(str) > (sizeof("ukhGHpppPSDI") - 1))
		return -1;

	while (*p) {
		if (*p != 'p' && strchr(p + 1, *p))
			return -1;
		p++;
	}

	return 0;
}

int parse_events__modifier_event(struct list_head *list, char *str, bool add)
{
	struct perf_evsel *evsel;
	struct event_modifier mod;

	if (str == NULL)
		return 0;

	if (check_modifier(str))
		return -EINVAL;

	if (!add && get_event_modifier(&mod, str, NULL))
		return -EINVAL;

	__evlist__for_each_entry(list, evsel) {
		if (add && get_event_modifier(&mod, str, evsel))
			return -EINVAL;

		evsel->attr.exclude_user   = mod.eu;
		evsel->attr.exclude_kernel = mod.ek;
		evsel->attr.exclude_hv     = mod.eh;
		evsel->attr.precise_ip     = mod.precise;
		evsel->attr.exclude_host   = mod.eH;
		evsel->attr.exclude_guest  = mod.eG;
		evsel->attr.exclude_idle   = mod.eI;
		evsel->exclude_GH          = mod.exclude_GH;
		evsel->sample_read         = mod.sample_read;
		evsel->precise_max         = mod.precise_max;

		if (perf_evsel__is_group_leader(evsel))
			evsel->attr.pinned = mod.pinned;
	}

	return 0;
}

int parse_events_name(struct list_head *list, char *name)
{
	struct perf_evsel *evsel;

	__evlist__for_each_entry(list, evsel) {
		if (!evsel->name)
			evsel->name = strdup(name);
	}

	return 0;
}

static int
comp_pmu(const void *p1, const void *p2)
{
	struct perf_pmu_event_symbol *pmu1 = (struct perf_pmu_event_symbol *) p1;
	struct perf_pmu_event_symbol *pmu2 = (struct perf_pmu_event_symbol *) p2;

	return strcasecmp(pmu1->symbol, pmu2->symbol);
}

static void perf_pmu__parse_cleanup(void)
{
	if (perf_pmu_events_list_num > 0) {
		struct perf_pmu_event_symbol *p;
		int i;

		for (i = 0; i < perf_pmu_events_list_num; i++) {
			p = perf_pmu_events_list + i;
			free(p->symbol);
		}
		free(perf_pmu_events_list);
		perf_pmu_events_list = NULL;
		perf_pmu_events_list_num = 0;
	}
}

#define SET_SYMBOL(str, stype)		\
do {					\
	p->symbol = str;		\
	if (!p->symbol)			\
		goto err;		\
	p->type = stype;		\
} while (0)

/*
 * Read the pmu events list from sysfs
 * Save it into perf_pmu_events_list
 */
static void perf_pmu__parse_init(void)
{

	struct perf_pmu *pmu = NULL;
	struct perf_pmu_alias *alias;
	int len = 0;

	pmu = perf_pmu__find("cpu");
	if ((pmu == NULL) || list_empty(&pmu->aliases)) {
		perf_pmu_events_list_num = -1;
		return;
	}
	list_for_each_entry(alias, &pmu->aliases, list) {
		if (strchr(alias->name, '-'))
			len++;
		len++;
	}
	perf_pmu_events_list = malloc(sizeof(struct perf_pmu_event_symbol) * len);
	if (!perf_pmu_events_list)
		return;
	perf_pmu_events_list_num = len;

	len = 0;
	list_for_each_entry(alias, &pmu->aliases, list) {
		struct perf_pmu_event_symbol *p = perf_pmu_events_list + len;
		char *tmp = strchr(alias->name, '-');

		if (tmp != NULL) {
			SET_SYMBOL(strndup(alias->name, tmp - alias->name),
					PMU_EVENT_SYMBOL_PREFIX);
			p++;
			SET_SYMBOL(strdup(++tmp), PMU_EVENT_SYMBOL_SUFFIX);
			len += 2;
		} else {
			SET_SYMBOL(strdup(alias->name), PMU_EVENT_SYMBOL);
			len++;
		}
	}
	qsort(perf_pmu_events_list, len,
		sizeof(struct perf_pmu_event_symbol), comp_pmu);

	return;
err:
	perf_pmu__parse_cleanup();
}

enum perf_pmu_event_symbol_type
perf_pmu__parse_check(const char *name)
{
	struct perf_pmu_event_symbol p, *r;

	/* scan kernel pmu events from sysfs if needed */
	if (perf_pmu_events_list_num == 0)
		perf_pmu__parse_init();
	/*
	 * name "cpu" could be prefix of cpu-cycles or cpu// events.
	 * cpu-cycles has been handled by hardcode.
	 * So it must be cpu// events, not kernel pmu event.
	 */
	if ((perf_pmu_events_list_num <= 0) || !strcmp(name, "cpu"))
		return PMU_EVENT_SYMBOL_ERR;

	p.symbol = strdup(name);
	r = bsearch(&p, perf_pmu_events_list,
			(size_t) perf_pmu_events_list_num,
			sizeof(struct perf_pmu_event_symbol), comp_pmu);
	free(p.symbol);
	return r ? r->type : PMU_EVENT_SYMBOL_ERR;
}

static int parse_events__scanner(const char *str, void *data, int start_token)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = parse_events_lex_init_extra(start_token, &scanner);
	if (ret)
		return ret;

	buffer = parse_events__scan_string(str, scanner);

#ifdef PARSER_DEBUG
	parse_events_debug = 1;
#endif
	ret = parse_events_parse(data, scanner);

	parse_events__flush_buffer(buffer, scanner);
	parse_events__delete_buffer(buffer, scanner);
	parse_events_lex_destroy(scanner);
	return ret;
}

/*
 * parse event config string, return a list of event terms.
 */
int parse_events_terms(struct list_head *terms, const char *str)
{
	struct parse_events_terms data = {
		.terms = NULL,
	};
	int ret;

	ret = parse_events__scanner(str, &data, PE_START_TERMS);
	if (!ret) {
		list_splice(data.terms, terms);
		zfree(&data.terms);
		return 0;
	}

	parse_events_terms__delete(data.terms);
	return ret;
}

int parse_events(struct perf_evlist *evlist, const char *str,
		 struct parse_events_error *err)
{
	struct parse_events_evlist data = {
		.list   = LIST_HEAD_INIT(data.list),
		.idx    = evlist->nr_entries,
		.error  = err,
		.evlist = evlist,
	};
	int ret;

	ret = parse_events__scanner(str, &data, PE_START_EVENTS);
	perf_pmu__parse_cleanup();
	if (!ret) {
		struct perf_evsel *last;

		if (list_empty(&data.list)) {
			WARN_ONCE(true, "WARNING: event parser found nothing");
			return -1;
		}

		perf_evlist__splice_list_tail(evlist, &data.list);
		evlist->nr_groups += data.nr_groups;
		last = perf_evlist__last(evlist);
		last->cmdline_group_boundary = true;

		return 0;
	}

	/*
	 * There are 2 users - builtin-record and builtin-test objects.
	 * Both call perf_evlist__delete in case of error, so we dont
	 * need to bother.
	 */
	return ret;
}

#define MAX_WIDTH 1000
static int get_term_width(void)
{
	struct winsize ws;

	get_term_dimensions(&ws);
	return ws.ws_col > MAX_WIDTH ? MAX_WIDTH : ws.ws_col;
}

static void parse_events_print_error(struct parse_events_error *err,
				     const char *event)
{
	const char *str = "invalid or unsupported event: ";
	char _buf[MAX_WIDTH];
	char *buf = (char *) event;
	int idx = 0;

	if (err->str) {
		/* -2 for extra '' in the final fprintf */
		int width       = get_term_width() - 2;
		int len_event   = strlen(event);
		int len_str, max_len, cut = 0;

		/*
		 * Maximum error index indent, we will cut
		 * the event string if it's bigger.
		 */
		int max_err_idx = 13;

		/*
		 * Let's be specific with the message when
		 * we have the precise error.
		 */
		str     = "event syntax error: ";
		len_str = strlen(str);
		max_len = width - len_str;

		buf = _buf;

		/* We're cutting from the beginning. */
		if (err->idx > max_err_idx)
			cut = err->idx - max_err_idx;

		strncpy(buf, event + cut, max_len);

		/* Mark cut parts with '..' on both sides. */
		if (cut)
			buf[0] = buf[1] = '.';

		if ((len_event - cut) > max_len) {
			buf[max_len - 1] = buf[max_len - 2] = '.';
			buf[max_len] = 0;
		}

		idx = len_str + err->idx - cut;
	}

	fprintf(stderr, "%s'%s'\n", str, buf);
	if (idx) {
		fprintf(stderr, "%*s\\___ %s\n", idx + 1, "", err->str);
		if (err->help)
			fprintf(stderr, "\n%s\n", err->help);
		free(err->str);
		free(err->help);
	}

	fprintf(stderr, "Run 'perf list' for a list of valid events\n");
}

#undef MAX_WIDTH

int parse_events_option(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	struct parse_events_error err = { .idx = 0, };
	int ret = parse_events(evlist, str, &err);

	if (ret)
		parse_events_print_error(&err, str);

	return ret;
}

static int
foreach_evsel_in_last_glob(struct perf_evlist *evlist,
			   int (*func)(struct perf_evsel *evsel,
				       const void *arg),
			   const void *arg)
{
	struct perf_evsel *last = NULL;
	int err;

	/*
	 * Don't return when list_empty, give func a chance to report
	 * error when it found last == NULL.
	 *
	 * So no need to WARN here, let *func do this.
	 */
	if (evlist->nr_entries > 0)
		last = perf_evlist__last(evlist);

	do {
		err = (*func)(last, arg);
		if (err)
			return -1;
		if (!last)
			return 0;

		if (last->node.prev == &evlist->entries)
			return 0;
		last = list_entry(last->node.prev, struct perf_evsel, node);
	} while (!last->cmdline_group_boundary);

	return 0;
}

static int set_filter(struct perf_evsel *evsel, const void *arg)
{
	const char *str = arg;
	bool found = false;
	int nr_addr_filters = 0;
	struct perf_pmu *pmu = NULL;

	if (evsel == NULL)
		goto err;

	if (evsel->attr.type == PERF_TYPE_TRACEPOINT) {
		if (perf_evsel__append_tp_filter(evsel, str) < 0) {
			fprintf(stderr,
				"not enough memory to hold filter string\n");
			return -1;
		}

		return 0;
	}

	while ((pmu = perf_pmu__scan(pmu)) != NULL)
		if (pmu->type == evsel->attr.type) {
			found = true;
			break;
		}

	if (found)
		perf_pmu__scan_file(pmu, "nr_addr_filters",
				    "%d", &nr_addr_filters);

	if (!nr_addr_filters)
		goto err;

	if (perf_evsel__append_addr_filter(evsel, str) < 0) {
		fprintf(stderr,
			"not enough memory to hold filter string\n");
		return -1;
	}

	return 0;

err:
	fprintf(stderr,
		"--filter option should follow a -e tracepoint or HW tracer option\n");

	return -1;
}

int parse_filter(const struct option *opt, const char *str,
		 int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;

	return foreach_evsel_in_last_glob(evlist, set_filter,
					  (const void *)str);
}

static int add_exclude_perf_filter(struct perf_evsel *evsel,
				   const void *arg __maybe_unused)
{
	char new_filter[64];

	if (evsel == NULL || evsel->attr.type != PERF_TYPE_TRACEPOINT) {
		fprintf(stderr,
			"--exclude-perf option should follow a -e tracepoint option\n");
		return -1;
	}

	snprintf(new_filter, sizeof(new_filter), "common_pid != %d", getpid());

	if (perf_evsel__append_tp_filter(evsel, new_filter) < 0) {
		fprintf(stderr,
			"not enough memory to hold filter string\n");
		return -1;
	}

	return 0;
}

int exclude_perf(const struct option *opt,
		 const char *arg __maybe_unused,
		 int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;

	return foreach_evsel_in_last_glob(evlist, add_exclude_perf_filter,
					  NULL);
}

static const char * const event_type_descriptors[] = {
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
	"Raw hardware event descriptor",
	"Hardware breakpoint",
};

static int cmp_string(const void *a, const void *b)
{
	const char * const *as = a;
	const char * const *bs = b;

	return strcmp(*as, *bs);
}

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */

void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_dirent, *evt_dirent;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];
	char **evt_list = NULL;
	unsigned int evt_i = 0, evt_num = 0;
	bool evt_num_known = false;

restart:
	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return;

	if (evt_num_known) {
		evt_list = zalloc(sizeof(char *) * evt_num);
		if (!evt_list)
			goto out_close_sys_dir;
	}

	for_each_subsystem(sys_dir, sys_dirent) {
		if (subsys_glob != NULL &&
		    !strglobmatch(sys_dirent->d_name, subsys_glob))
			continue;

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent->d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent) {
			if (event_glob != NULL &&
			    !strglobmatch(evt_dirent->d_name, event_glob))
				continue;

			if (!evt_num_known) {
				evt_num++;
				continue;
			}

			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);

			evt_list[evt_i] = strdup(evt_path);
			if (evt_list[evt_i] == NULL)
				goto out_close_evt_dir;
			evt_i++;
		}
		closedir(evt_dir);
	}
	closedir(sys_dir);

	if (!evt_num_known) {
		evt_num_known = true;
		goto restart;
	}
	qsort(evt_list, evt_num, sizeof(char *), cmp_string);
	evt_i = 0;
	while (evt_i < evt_num) {
		if (name_only) {
			printf("%s ", evt_list[evt_i++]);
			continue;
		}
		printf("  %-50s [%s]\n", evt_list[evt_i++],
				event_type_descriptors[PERF_TYPE_TRACEPOINT]);
	}
	if (evt_num && pager_in_use())
		printf("\n");

out_free:
	evt_num = evt_i;
	for (evt_i = 0; evt_i < evt_num; evt_i++)
		zfree(&evt_list[evt_i]);
	zfree(&evt_list);
	return;

out_close_evt_dir:
	closedir(evt_dir);
out_close_sys_dir:
	closedir(sys_dir);

	printf("FATAL: not enough memory to print %s\n",
			event_type_descriptors[PERF_TYPE_TRACEPOINT]);
	if (evt_list)
		goto out_free;
}

/*
 * Check whether event is in <debugfs_mount_point>/tracing/events
 */

int is_valid_tracepoint(const char *event_string)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_dirent, *evt_dirent;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];

	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return 0;

	for_each_subsystem(sys_dir, sys_dirent) {

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent->d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent) {
			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);
			if (!strcmp(evt_path, event_string)) {
				closedir(evt_dir);
				closedir(sys_dir);
				return 1;
			}
		}
		closedir(evt_dir);
	}
	closedir(sys_dir);
	return 0;
}

static bool is_event_supported(u8 type, unsigned config)
{
	bool ret = true;
	int open_return;
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type = type,
		.config = config,
		.disabled = 1,
	};
	struct {
		struct thread_map map;
		int threads[1];
	} tmap = {
		.map.nr	 = 1,
		.threads = { 0 },
	};

	evsel = perf_evsel__new(&attr);
	if (evsel) {
		open_return = perf_evsel__open(evsel, NULL, &tmap.map);
		ret = open_return >= 0;

		if (open_return == -EACCES) {
			/*
			 * This happens if the paranoid value
			 * /proc/sys/kernel/perf_event_paranoid is set to 2
			 * Re-run with exclude_kernel set; we don't do that
			 * by default as some ARM machines do not support it.
			 *
			 */
			evsel->attr.exclude_kernel = 1;
			ret = perf_evsel__open(evsel, NULL, &tmap.map) >= 0;
		}
		perf_evsel__delete(evsel);
	}

	return ret;
}

void print_sdt_events(const char *subsys_glob, const char *event_glob,
		      bool name_only)
{
	struct probe_cache *pcache;
	struct probe_cache_entry *ent;
	struct strlist *bidlist, *sdtlist;
	struct strlist_config cfg = {.dont_dupstr = true};
	struct str_node *nd, *nd2;
	char *buf, *path, *ptr = NULL;
	bool show_detail = false;
	int ret;

	sdtlist = strlist__new(NULL, &cfg);
	if (!sdtlist) {
		pr_debug("Failed to allocate new strlist for SDT\n");
		return;
	}
	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		pr_debug("Failed to get buildids: %d\n", errno);
		return;
	}
	strlist__for_each_entry(nd, bidlist) {
		pcache = probe_cache__new(nd->s);
		if (!pcache)
			continue;
		list_for_each_entry(ent, &pcache->entries, node) {
			if (!ent->sdt)
				continue;
			if (subsys_glob &&
			    !strglobmatch(ent->pev.group, subsys_glob))
				continue;
			if (event_glob &&
			    !strglobmatch(ent->pev.event, event_glob))
				continue;
			ret = asprintf(&buf, "%s:%s@%s", ent->pev.group,
					ent->pev.event, nd->s);
			if (ret > 0)
				strlist__add(sdtlist, buf);
		}
		probe_cache__delete(pcache);
	}
	strlist__delete(bidlist);

	strlist__for_each_entry(nd, sdtlist) {
		buf = strchr(nd->s, '@');
		if (buf)
			*(buf++) = '\0';
		if (name_only) {
			printf("%s ", nd->s);
			continue;
		}
		nd2 = strlist__next(nd);
		if (nd2) {
			ptr = strchr(nd2->s, '@');
			if (ptr)
				*ptr = '\0';
			if (strcmp(nd->s, nd2->s) == 0)
				show_detail = true;
		}
		if (show_detail) {
			path = build_id_cache__origname(buf);
			ret = asprintf(&buf, "%s@%s(%.12s)", nd->s, path, buf);
			if (ret > 0) {
				printf("  %-50s [%s]\n", buf, "SDT event");
				free(buf);
			}
		} else
			printf("  %-50s [%s]\n", nd->s, "SDT event");
		if (nd2) {
			if (strcmp(nd->s, nd2->s) != 0)
				show_detail = false;
			if (ptr)
				*ptr = '@';
		}
	}
	strlist__delete(sdtlist);
}

int print_hwcache_events(const char *event_glob, bool name_only)
{
	unsigned int type, op, i, evt_i = 0, evt_num = 0;
	char name[64];
	char **evt_list = NULL;
	bool evt_num_known = false;

restart:
	if (evt_num_known) {
		evt_list = zalloc(sizeof(char *) * evt_num);
		if (!evt_list)
			goto out_enomem;
	}

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				if (event_glob != NULL && !strglobmatch(name, event_glob))
					continue;

				if (!is_event_supported(PERF_TYPE_HW_CACHE,
							type | (op << 8) | (i << 16)))
					continue;

				if (!evt_num_known) {
					evt_num++;
					continue;
				}

				evt_list[evt_i] = strdup(name);
				if (evt_list[evt_i] == NULL)
					goto out_enomem;
				evt_i++;
			}
		}
	}

	if (!evt_num_known) {
		evt_num_known = true;
		goto restart;
	}
	qsort(evt_list, evt_num, sizeof(char *), cmp_string);
	evt_i = 0;
	while (evt_i < evt_num) {
		if (name_only) {
			printf("%s ", evt_list[evt_i++]);
			continue;
		}
		printf("  %-50s [%s]\n", evt_list[evt_i++],
				event_type_descriptors[PERF_TYPE_HW_CACHE]);
	}
	if (evt_num && pager_in_use())
		printf("\n");

out_free:
	evt_num = evt_i;
	for (evt_i = 0; evt_i < evt_num; evt_i++)
		zfree(&evt_list[evt_i]);
	zfree(&evt_list);
	return evt_num;

out_enomem:
	printf("FATAL: not enough memory to print %s\n", event_type_descriptors[PERF_TYPE_HW_CACHE]);
	if (evt_list)
		goto out_free;
	return evt_num;
}

void print_symbol_events(const char *event_glob, unsigned type,
				struct event_symbol *syms, unsigned max,
				bool name_only)
{
	unsigned int i, evt_i = 0, evt_num = 0;
	char name[MAX_NAME_LEN];
	char **evt_list = NULL;
	bool evt_num_known = false;

restart:
	if (evt_num_known) {
		evt_list = zalloc(sizeof(char *) * evt_num);
		if (!evt_list)
			goto out_enomem;
		syms -= max;
	}

	for (i = 0; i < max; i++, syms++) {

		if (event_glob != NULL && syms->symbol != NULL &&
		    !(strglobmatch(syms->symbol, event_glob) ||
		      (syms->alias && strglobmatch(syms->alias, event_glob))))
			continue;

		if (!is_event_supported(type, i))
			continue;

		if (!evt_num_known) {
			evt_num++;
			continue;
		}

		if (!name_only && strlen(syms->alias))
			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms->symbol, syms->alias);
		else
			strncpy(name, syms->symbol, MAX_NAME_LEN);

		evt_list[evt_i] = strdup(name);
		if (evt_list[evt_i] == NULL)
			goto out_enomem;
		evt_i++;
	}

	if (!evt_num_known) {
		evt_num_known = true;
		goto restart;
	}
	qsort(evt_list, evt_num, sizeof(char *), cmp_string);
	evt_i = 0;
	while (evt_i < evt_num) {
		if (name_only) {
			printf("%s ", evt_list[evt_i++]);
			continue;
		}
		printf("  %-50s [%s]\n", evt_list[evt_i++], event_type_descriptors[type]);
	}
	if (evt_num && pager_in_use())
		printf("\n");

out_free:
	evt_num = evt_i;
	for (evt_i = 0; evt_i < evt_num; evt_i++)
		zfree(&evt_list[evt_i]);
	zfree(&evt_list);
	return;

out_enomem:
	printf("FATAL: not enough memory to print %s\n", event_type_descriptors[type]);
	if (evt_list)
		goto out_free;
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const char *event_glob, bool name_only, bool quiet_flag,
			bool long_desc)
{
	print_symbol_events(event_glob, PERF_TYPE_HARDWARE,
			    event_symbols_hw, PERF_COUNT_HW_MAX, name_only);

	print_symbol_events(event_glob, PERF_TYPE_SOFTWARE,
			    event_symbols_sw, PERF_COUNT_SW_MAX, name_only);

	print_hwcache_events(event_glob, name_only);

	print_pmu_events(event_glob, name_only, quiet_flag, long_desc);

	if (event_glob != NULL)
		return;

	if (!name_only) {
		printf("  %-50s [%s]\n",
		       "rNNN",
		       event_type_descriptors[PERF_TYPE_RAW]);
		printf("  %-50s [%s]\n",
		       "cpu/t1=v1[,t2=v2,t3 ...]/modifier",
		       event_type_descriptors[PERF_TYPE_RAW]);
		if (pager_in_use())
			printf("   (see 'man perf-list' on how to encode it)\n\n");

		printf("  %-50s [%s]\n",
		       "mem:<addr>[/len][:access]",
			event_type_descriptors[PERF_TYPE_BREAKPOINT]);
		if (pager_in_use())
			printf("\n");
	}

	print_tracepoint_events(NULL, NULL, name_only);

	print_sdt_events(NULL, NULL, name_only);
}

int parse_events__is_hardcoded_term(struct parse_events_term *term)
{
	return term->type_term != PARSE_EVENTS__TERM_TYPE_USER;
}

static int new_term(struct parse_events_term **_term, int type_val,
		    int type_term, char *config,
		    char *str, u64 num, int err_term, int err_val)
{
	struct parse_events_term *term;

	term = zalloc(sizeof(*term));
	if (!term)
		return -ENOMEM;

	INIT_LIST_HEAD(&term->list);
	term->type_val  = type_val;
	term->type_term = type_term;
	term->config = config;
	term->err_term = err_term;
	term->err_val  = err_val;

	switch (type_val) {
	case PARSE_EVENTS__TERM_TYPE_NUM:
		term->val.num = num;
		break;
	case PARSE_EVENTS__TERM_TYPE_STR:
		term->val.str = str;
		break;
	default:
		free(term);
		return -EINVAL;
	}

	*_term = term;
	return 0;
}

int parse_events_term__num(struct parse_events_term **term,
			   int type_term, char *config, u64 num,
			   void *loc_term_, void *loc_val_)
{
	YYLTYPE *loc_term = loc_term_;
	YYLTYPE *loc_val = loc_val_;

	return new_term(term, PARSE_EVENTS__TERM_TYPE_NUM, type_term,
			config, NULL, num,
			loc_term ? loc_term->first_column : 0,
			loc_val ? loc_val->first_column : 0);
}

int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str,
			   void *loc_term_, void *loc_val_)
{
	YYLTYPE *loc_term = loc_term_;
	YYLTYPE *loc_val = loc_val_;

	return new_term(term, PARSE_EVENTS__TERM_TYPE_STR, type_term,
			config, str, 0,
			loc_term ? loc_term->first_column : 0,
			loc_val ? loc_val->first_column : 0);
}

int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx)
{
	struct event_symbol *sym;

	BUG_ON(idx >= PERF_COUNT_HW_MAX);
	sym = &event_symbols_hw[idx];

	if (config)
		return new_term(term, PARSE_EVENTS__TERM_TYPE_STR,
				PARSE_EVENTS__TERM_TYPE_USER, config,
				(char *) sym->symbol, 0, 0, 0);
	else
		return new_term(term, PARSE_EVENTS__TERM_TYPE_STR,
				PARSE_EVENTS__TERM_TYPE_USER,
				(char *) "event", (char *) sym->symbol,
				0, 0, 0);
}

int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term)
{
	return new_term(new, term->type_val, term->type_term, term->config,
			term->val.str, term->val.num,
			term->err_term, term->err_val);
}

void parse_events_terms__purge(struct list_head *terms)
{
	struct parse_events_term *term, *h;

	list_for_each_entry_safe(term, h, terms, list) {
		if (term->array.nr_ranges)
			free(term->array.ranges);
		list_del_init(&term->list);
		free(term);
	}
}

void parse_events_terms__delete(struct list_head *terms)
{
	if (!terms)
		return;
	parse_events_terms__purge(terms);
	free(terms);
}

void parse_events__clear_array(struct parse_events_array *a)
{
	free(a->ranges);
}

void parse_events_evlist_error(struct parse_events_evlist *data,
			       int idx, const char *str)
{
	struct parse_events_error *err = data->error;

	if (!err)
		return;
	err->idx = idx;
	err->str = strdup(str);
	WARN_ONCE(!err->str, "WARNING: failed to allocate error string");
}

static void config_terms_list(char *buf, size_t buf_sz)
{
	int i;
	bool first = true;

	buf[0] = '\0';
	for (i = 0; i < __PARSE_EVENTS__TERM_TYPE_NR; i++) {
		const char *name = config_term_names[i];

		if (!config_term_avail(i, NULL))
			continue;
		if (!name)
			continue;
		if (name[0] == '<')
			continue;

		if (strlen(buf) + strlen(name) + 2 >= buf_sz)
			return;

		if (!first)
			strcat(buf, ",");
		else
			first = false;
		strcat(buf, name);
	}
}

/*
 * Return string contains valid config terms of an event.
 * @additional_terms: For terms such as PMU sysfs terms.
 */
char *parse_events_formats_error_string(char *additional_terms)
{
	char *str;
	/* "no-overwrite" is the longest name */
	char static_terms[__PARSE_EVENTS__TERM_TYPE_NR *
			  (sizeof("no-overwrite") - 1)];

	config_terms_list(static_terms, sizeof(static_terms));
	/* valid terms */
	if (additional_terms) {
		if (asprintf(&str, "valid terms: %s,%s",
			     additional_terms, static_terms) < 0)
			goto fail;
	} else {
		if (asprintf(&str, "valid terms: %s", static_terms) < 0)
			goto fail;
	}
	return str;

fail:
	return NULL;
}

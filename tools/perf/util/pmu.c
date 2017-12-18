#include <linux/list.h>
#include <linux/compiler.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dirent.h>
#include <api/fs/fs.h>
#include <locale.h>
#include "util.h"
#include "pmu.h"
#include "parse-events.h"
#include "cpumap.h"
#include "header.h"
#include "pmu-events/pmu-events.h"
#include "cache.h"

struct perf_pmu_format {
	char *name;
	int value;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	struct list_head list;
};

#define EVENT_SOURCE_DEVICE_PATH "/bus/event_source/devices/"

int perf_pmu_parse(struct list_head *list, char *name);
extern FILE *perf_pmu_in;

static LIST_HEAD(pmus);

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
int perf_pmu__format_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *format_dir;
	int ret = 0;

	format_dir = opendir(dir);
	if (!format_dir)
		return -EINVAL;

	while (!ret && (evt_ent = readdir(format_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", dir, name);

		ret = -EINVAL;
		file = fopen(path, "r");
		if (!file)
			break;

		perf_pmu_in = file;
		ret = perf_pmu_parse(head, name);
		fclose(file);
	}

	closedir(format_dir);
	return ret;
}

/*
 * Reading/parsing the default pmu format definition, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/format as sysfs group attributes.
 */
static int pmu_format(const char *name, struct list_head *format)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/format", sysfs, name);

	if (stat(path, &st) < 0)
		return 0;	/* no error if format does not exist */

	if (perf_pmu__format_parse(path, format))
		return -1;

	return 0;
}

static int perf_pmu__parse_scale(struct perf_pmu_alias *alias, char *dir, char *name)
{
	struct stat st;
	ssize_t sret;
	char scale[128];
	int fd, ret = -1;
	char path[PATH_MAX];
	char *lc;

	snprintf(path, PATH_MAX, "%s/%s.scale", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	if (fstat(fd, &st) < 0)
		goto error;

	sret = read(fd, scale, sizeof(scale)-1);
	if (sret < 0)
		goto error;

	if (scale[sret - 1] == '\n')
		scale[sret - 1] = '\0';
	else
		scale[sret] = '\0';

	/*
	 * save current locale
	 */
	lc = setlocale(LC_NUMERIC, NULL);

	/*
	 * The lc string may be allocated in static storage,
	 * so get a dynamic copy to make it survive setlocale
	 * call below.
	 */
	lc = strdup(lc);
	if (!lc) {
		ret = -ENOMEM;
		goto error;
	}

	/*
	 * force to C locale to ensure kernel
	 * scale string is converted correctly.
	 * kernel uses default C locale.
	 */
	setlocale(LC_NUMERIC, "C");

	alias->scale = strtod(scale, NULL);

	/* restore locale */
	setlocale(LC_NUMERIC, lc);

	free(lc);

	ret = 0;
error:
	close(fd);
	return ret;
}

static int perf_pmu__parse_unit(struct perf_pmu_alias *alias, char *dir, char *name)
{
	char path[PATH_MAX];
	ssize_t sret;
	int fd;

	snprintf(path, PATH_MAX, "%s/%s.unit", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	sret = read(fd, alias->unit, UNIT_MAX_LEN);
	if (sret < 0)
		goto error;

	close(fd);

	if (alias->unit[sret - 1] == '\n')
		alias->unit[sret - 1] = '\0';
	else
		alias->unit[sret] = '\0';

	return 0;
error:
	close(fd);
	alias->unit[0] = '\0';
	return -1;
}

static int
perf_pmu__parse_per_pkg(struct perf_pmu_alias *alias, char *dir, char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, PATH_MAX, "%s/%s.per-pkg", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	close(fd);

	alias->per_pkg = true;
	return 0;
}

static int perf_pmu__parse_snapshot(struct perf_pmu_alias *alias,
				    char *dir, char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, PATH_MAX, "%s/%s.snapshot", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	alias->snapshot = true;
	close(fd);
	return 0;
}

static int __perf_pmu__new_alias(struct list_head *list, char *dir, char *name,
				 char *desc, char *val, char *long_desc,
				 char *topic)
{
	struct perf_pmu_alias *alias;
	int ret;

	alias = malloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;

	INIT_LIST_HEAD(&alias->terms);
	alias->scale = 1.0;
	alias->unit[0] = '\0';
	alias->per_pkg = false;
	alias->snapshot = false;

	ret = parse_events_terms(&alias->terms, val);
	if (ret) {
		pr_err("Cannot parse alias %s: %d\n", val, ret);
		free(alias);
		return ret;
	}

	alias->name = strdup(name);
	if (dir) {
		/*
		 * load unit name and scale if available
		 */
		perf_pmu__parse_unit(alias, dir, name);
		perf_pmu__parse_scale(alias, dir, name);
		perf_pmu__parse_per_pkg(alias, dir, name);
		perf_pmu__parse_snapshot(alias, dir, name);
	}

	alias->desc = desc ? strdup(desc) : NULL;
	alias->long_desc = long_desc ? strdup(long_desc) :
				desc ? strdup(desc) : NULL;
	alias->topic = topic ? strdup(topic) : NULL;

	list_add_tail(&alias->list, list);

	return 0;
}

static int perf_pmu__new_alias(struct list_head *list, char *dir, char *name, FILE *file)
{
	char buf[256];
	int ret;

	ret = fread(buf, 1, sizeof(buf), file);
	if (ret == 0)
		return -EINVAL;

	buf[ret] = 0;

	return __perf_pmu__new_alias(list, dir, name, NULL, buf, NULL, NULL);
}

static inline bool pmu_alias_info_file(char *name)
{
	size_t len;

	len = strlen(name);
	if (len > 5 && !strcmp(name + len - 5, ".unit"))
		return true;
	if (len > 6 && !strcmp(name + len - 6, ".scale"))
		return true;
	if (len > 8 && !strcmp(name + len - 8, ".per-pkg"))
		return true;
	if (len > 9 && !strcmp(name + len - 9, ".snapshot"))
		return true;

	return false;
}

/*
 * Process all the sysfs attributes located under the directory
 * specified in 'dir' parameter.
 */
static int pmu_aliases_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *event_dir;

	event_dir = opendir(dir);
	if (!event_dir)
		return -EINVAL;

	while ((evt_ent = readdir(event_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/*
		 * skip info files parsed in perf_pmu__new_alias()
		 */
		if (pmu_alias_info_file(name))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", dir, name);

		file = fopen(path, "r");
		if (!file) {
			pr_debug("Cannot open %s\n", path);
			continue;
		}

		if (perf_pmu__new_alias(head, dir, name, file) < 0)
			pr_debug("Cannot set up %s\n", name);
		fclose(file);
	}

	closedir(event_dir);
	return 0;
}

/*
 * Reading the pmu event aliases definition, which should be located at:
 * /sys/bus/event_source/devices/<dev>/events as sysfs group attributes.
 */
static int pmu_aliases(const char *name, struct list_head *head)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/events", sysfs, name);

	if (stat(path, &st) < 0)
		return 0;	 /* no error if 'events' does not exist */

	if (pmu_aliases_parse(path, head))
		return -1;

	return 0;
}

static int pmu_alias_terms(struct perf_pmu_alias *alias,
			   struct list_head *terms)
{
	struct parse_events_term *term, *cloned;
	LIST_HEAD(list);
	int ret;

	list_for_each_entry(term, &alias->terms, list) {
		ret = parse_events_term__clone(&cloned, term);
		if (ret) {
			parse_events_terms__purge(&list);
			return ret;
		}
		list_add_tail(&cloned->list, &list);
	}
	list_splice(&list, terms);
	return 0;
}

/*
 * Reading/parsing the default pmu type value, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/type as sysfs attribute.
 */
static int pmu_type(const char *name, __u32 *type)
{
	struct stat st;
	char path[PATH_MAX];
	FILE *file;
	int ret = 0;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/type", sysfs, name);

	if (stat(path, &st) < 0)
		return -1;

	file = fopen(path, "r");
	if (!file)
		return -EINVAL;

	if (1 != fscanf(file, "%u", type))
		ret = -1;

	fclose(file);
	return ret;
}

/* Add all pmus in sysfs to pmu list: */
static void pmu_read_sysfs(void)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;
		/* add to static LIST_HEAD(pmus): */
		perf_pmu__find(dent->d_name);
	}

	closedir(dir);
}

static struct cpu_map *pmu_cpumask(const char *name)
{
	struct stat st;
	char path[PATH_MAX];
	FILE *file;
	struct cpu_map *cpus;
	const char *sysfs = sysfs__mountpoint();
	const char *templates[] = {
		 "%s/bus/event_source/devices/%s/cpumask",
		 "%s/bus/event_source/devices/%s/cpus",
		 NULL
	};
	const char **template;

	if (!sysfs)
		return NULL;

	for (template = templates; *template; template++) {
		snprintf(path, PATH_MAX, *template, sysfs, name);
		if (stat(path, &st) == 0)
			break;
	}

	if (!*template)
		return NULL;

	file = fopen(path, "r");
	if (!file)
		return NULL;

	cpus = cpu_map__read(file);
	fclose(file);
	return cpus;
}

/*
 * Return the CPU id as a raw string.
 *
 * Each architecture should provide a more precise id string that
 * can be use to match the architecture's "mapfile".
 */
char * __weak get_cpuid_str(void)
{
	return NULL;
}

/*
 * From the pmu_events_map, find the table of PMU events that corresponds
 * to the current running CPU. Then, add all PMU events from that table
 * as aliases.
 */
static void pmu_add_cpu_aliases(struct list_head *head)
{
	int i;
	struct pmu_events_map *map;
	struct pmu_event *pe;
	char *cpuid;

	cpuid = getenv("PERF_CPUID");
	if (cpuid)
		cpuid = strdup(cpuid);
	if (!cpuid)
		cpuid = get_cpuid_str();
	if (!cpuid)
		return;

	pr_debug("Using CPUID %s\n", cpuid);

	i = 0;
	while (1) {
		map = &pmu_events_map[i++];
		if (!map->table)
			goto out;

		if (!strcmp(map->cpuid, cpuid))
			break;
	}

	/*
	 * Found a matching PMU events table. Create aliases
	 */
	i = 0;
	while (1) {
		pe = &map->table[i++];
		if (!pe->name)
			break;

		/* need type casts to override 'const' */
		__perf_pmu__new_alias(head, NULL, (char *)pe->name,
				(char *)pe->desc, (char *)pe->event,
				(char *)pe->long_desc, (char *)pe->topic);
	}

out:
	free(cpuid);
}

struct perf_event_attr * __weak
perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
	return NULL;
}

static struct perf_pmu *pmu_lookup(const char *name)
{
	struct perf_pmu *pmu;
	LIST_HEAD(format);
	LIST_HEAD(aliases);
	__u32 type;

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(name, &format))
		return NULL;

	if (pmu_aliases(name, &aliases))
		return NULL;

	if (!strcmp(name, "cpu"))
		pmu_add_cpu_aliases(&aliases);

	if (pmu_type(name, &type))
		return NULL;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	pmu->cpus = pmu_cpumask(name);

	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->aliases);
	list_splice(&format, &pmu->format);
	list_splice(&aliases, &pmu->aliases);
	pmu->name = strdup(name);
	pmu->type = type;
	list_add_tail(&pmu->list, &pmus);

	pmu->default_config = perf_pmu__get_default_config(pmu);

	return pmu;
}

static struct perf_pmu *pmu_find(const char *name)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &pmus, list)
		if (!strcmp(pmu->name, name))
			return pmu;

	return NULL;
}

struct perf_pmu *perf_pmu__scan(struct perf_pmu *pmu)
{
	/*
	 * pmu iterator: If pmu is NULL, we start at the begin,
	 * otherwise return the next pmu. Returns NULL on end.
	 */
	if (!pmu) {
		pmu_read_sysfs();
		pmu = list_prepare_entry(pmu, &pmus, list);
	}
	list_for_each_entry_continue(pmu, &pmus, list)
		return pmu;
	return NULL;
}

struct perf_pmu *perf_pmu__find(const char *name)
{
	struct perf_pmu *pmu;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	return pmu_lookup(name);
}

static struct perf_pmu_format *
pmu_find_format(struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format;

	list_for_each_entry(format, formats, list)
		if (!strcmp(format->name, name))
			return format;

	return NULL;
}

__u64 perf_pmu__format_bits(struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format = pmu_find_format(formats, name);
	__u64 bits = 0;
	int fbit;

	if (!format)
		return 0;

	for_each_set_bit(fbit, format->bits, PERF_PMU_FORMAT_BITS)
		bits |= 1ULL << fbit;

	return bits;
}

/*
 * Sets value based on the format definition (format parameter)
 * and unformated value (value parameter).
 */
static void pmu_format_value(unsigned long *format, __u64 value, __u64 *v,
			     bool zero)
{
	unsigned long fbit, vbit;

	for (fbit = 0, vbit = 0; fbit < PERF_PMU_FORMAT_BITS; fbit++) {

		if (!test_bit(fbit, format))
			continue;

		if (value & (1llu << vbit++))
			*v |= (1llu << fbit);
		else if (zero)
			*v &= ~(1llu << fbit);
	}
}

static __u64 pmu_format_max_value(const unsigned long *format)
{
	__u64 w = 0;
	int fbit;

	for_each_set_bit(fbit, format, PERF_PMU_FORMAT_BITS)
		w |= (1ULL << fbit);

	return w;
}

/*
 * Term is a string term, and might be a param-term. Try to look up it's value
 * in the remaining terms.
 * - We have a term like "base-or-format-term=param-term",
 * - We need to find the value supplied for "param-term" (with param-term named
 *   in a config string) later on in the term list.
 */
static int pmu_resolve_param_term(struct parse_events_term *term,
				  struct list_head *head_terms,
				  __u64 *value)
{
	struct parse_events_term *t;

	list_for_each_entry(t, head_terms, list) {
		if (t->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
			if (!strcmp(t->config, term->config)) {
				t->used = true;
				*value = t->val.num;
				return 0;
			}
		}
	}

	if (verbose)
		printf("Required parameter '%s' not specified\n", term->config);

	return -1;
}

static char *pmu_formats_string(struct list_head *formats)
{
	struct perf_pmu_format *format;
	char *str = NULL;
	struct strbuf buf = STRBUF_INIT;
	unsigned i = 0;

	if (!formats)
		return NULL;

	/* sysfs exported terms */
	list_for_each_entry(format, formats, list)
		if (strbuf_addf(&buf, i++ ? ",%s" : "%s", format->name) < 0)
			goto error;

	str = strbuf_detach(&buf, NULL);
error:
	strbuf_release(&buf);

	return str;
}

/*
 * Setup one of config[12] attr members based on the
 * user input data - term parameter.
 */
static int pmu_config_term(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct list_head *head_terms,
			   bool zero, struct parse_events_error *err)
{
	struct perf_pmu_format *format;
	__u64 *vp;
	__u64 val, max_val;

	/*
	 * If this is a parameter we've already used for parameterized-eval,
	 * skip it in normal eval.
	 */
	if (term->used)
		return 0;

	/*
	 * Hardcoded terms should be already in, so nothing
	 * to be done for them.
	 */
	if (parse_events__is_hardcoded_term(term))
		return 0;

	format = pmu_find_format(formats, term->config);
	if (!format) {
		if (verbose)
			printf("Invalid event/parameter '%s'\n", term->config);
		if (err) {
			char *pmu_term = pmu_formats_string(formats);

			err->idx  = term->err_term;
			err->str  = strdup("unknown term");
			err->help = parse_events_formats_error_string(pmu_term);
			free(pmu_term);
		}
		return -EINVAL;
	}

	switch (format->value) {
	case PERF_PMU_FORMAT_VALUE_CONFIG:
		vp = &attr->config;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG1:
		vp = &attr->config1;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG2:
		vp = &attr->config2;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Either directly use a numeric term, or try to translate string terms
	 * using event parameters.
	 */
	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
		val = term->val.num;
	else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
		if (strcmp(term->val.str, "?")) {
			if (verbose) {
				pr_info("Invalid sysfs entry %s=%s\n",
						term->config, term->val.str);
			}
			if (err) {
				err->idx = term->err_val;
				err->str = strdup("expected numeric value");
			}
			return -EINVAL;
		}

		if (pmu_resolve_param_term(term, head_terms, &val))
			return -EINVAL;
	} else
		return -EINVAL;

	max_val = pmu_format_max_value(format->bits);
	if (val > max_val) {
		if (err) {
			err->idx = term->err_val;
			if (asprintf(&err->str,
				     "value too big for format, maximum is %llu",
				     (unsigned long long)max_val) < 0)
				err->str = strdup("value too big for format");
			return -EINVAL;
		}
		/*
		 * Assume we don't care if !err, in which case the value will be
		 * silently truncated.
		 */
	}

	pmu_format_value(format->bits, val, vp, zero);
	return 0;
}

int perf_pmu__config_terms(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct list_head *head_terms,
			   bool zero, struct parse_events_error *err)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head_terms, list) {
		if (pmu_config_term(formats, attr, term, head_terms,
				    zero, err))
			return -EINVAL;
	}

	return 0;
}

/*
 * Configures event's 'attr' parameter based on the:
 * 1) users input - specified in terms parameter
 * 2) pmu format definitions - specified by pmu parameter
 */
int perf_pmu__config(struct perf_pmu *pmu, struct perf_event_attr *attr,
		     struct list_head *head_terms,
		     struct parse_events_error *err)
{
	bool zero = !!pmu->default_config;

	attr->type = pmu->type;
	return perf_pmu__config_terms(&pmu->format, attr, head_terms,
				      zero, err);
}

static struct perf_pmu_alias *pmu_find_alias(struct perf_pmu *pmu,
					     struct parse_events_term *term)
{
	struct perf_pmu_alias *alias;
	char *name;

	if (parse_events__is_hardcoded_term(term))
		return NULL;

	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
		if (term->val.num != 1)
			return NULL;
		if (pmu_find_format(&pmu->format, term->config))
			return NULL;
		name = term->config;
	} else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
		if (strcasecmp(term->config, "event"))
			return NULL;
		name = term->val.str;
	} else {
		return NULL;
	}

	list_for_each_entry(alias, &pmu->aliases, list) {
		if (!strcasecmp(alias->name, name))
			return alias;
	}
	return NULL;
}


static int check_info_data(struct perf_pmu_alias *alias,
			   struct perf_pmu_info *info)
{
	/*
	 * Only one term in event definition can
	 * define unit, scale and snapshot, fail
	 * if there's more than one.
	 */
	if ((info->unit && alias->unit) ||
	    (info->scale && alias->scale) ||
	    (info->snapshot && alias->snapshot))
		return -EINVAL;

	if (alias->unit)
		info->unit = alias->unit;

	if (alias->scale)
		info->scale = alias->scale;

	if (alias->snapshot)
		info->snapshot = alias->snapshot;

	return 0;
}

/*
 * Find alias in the terms list and replace it with the terms
 * defined for the alias
 */
int perf_pmu__check_alias(struct perf_pmu *pmu, struct list_head *head_terms,
			  struct perf_pmu_info *info)
{
	struct parse_events_term *term, *h;
	struct perf_pmu_alias *alias;
	int ret;

	info->per_pkg = false;

	/*
	 * Mark unit and scale as not set
	 * (different from default values, see below)
	 */
	info->unit     = NULL;
	info->scale    = 0.0;
	info->snapshot = false;

	list_for_each_entry_safe(term, h, head_terms, list) {
		alias = pmu_find_alias(pmu, term);
		if (!alias)
			continue;
		ret = pmu_alias_terms(alias, &term->list);
		if (ret)
			return ret;

		ret = check_info_data(alias, info);
		if (ret)
			return ret;

		if (alias->per_pkg)
			info->per_pkg = true;

		list_del(&term->list);
		free(term);
	}

	/*
	 * if no unit or scale foundin aliases, then
	 * set defaults as for evsel
	 * unit cannot left to NULL
	 */
	if (info->unit == NULL)
		info->unit   = "";

	if (info->scale == 0.0)
		info->scale  = 1.0;

	return 0;
}

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits)
{
	struct perf_pmu_format *format;

	format = zalloc(sizeof(*format));
	if (!format)
		return -ENOMEM;

	format->name = strdup(name);
	format->value = config;
	memcpy(format->bits, bits, sizeof(format->bits));

	list_add_tail(&format->list, list);
	return 0;
}

void perf_pmu__set_format(unsigned long *bits, long from, long to)
{
	long b;

	if (!to)
		to = from;

	memset(bits, 0, BITS_TO_BYTES(PERF_PMU_FORMAT_BITS));
	for (b = from; b <= to; b++)
		set_bit(b, bits);
}

static int sub_non_neg(int a, int b)
{
	if (b > a)
		return 0;
	return a - b;
}

static char *format_alias(char *buf, int len, struct perf_pmu *pmu,
			  struct perf_pmu_alias *alias)
{
	struct parse_events_term *term;
	int used = snprintf(buf, len, "%s/%s", pmu->name, alias->name);

	list_for_each_entry(term, &alias->terms, list) {
		if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR)
			used += snprintf(buf + used, sub_non_neg(len, used),
					",%s=%s", term->config,
					term->val.str);
	}

	if (sub_non_neg(len, used) > 0) {
		buf[used] = '/';
		used++;
	}
	if (sub_non_neg(len, used) > 0) {
		buf[used] = '\0';
		used++;
	} else
		buf[len - 1] = '\0';

	return buf;
}

static char *format_alias_or(char *buf, int len, struct perf_pmu *pmu,
			     struct perf_pmu_alias *alias)
{
	snprintf(buf, len, "%s OR %s/%s/", alias->name, pmu->name, alias->name);
	return buf;
}

struct sevent {
	char *name;
	char *desc;
	char *topic;
};

static int cmp_sevent(const void *a, const void *b)
{
	const struct sevent *as = a;
	const struct sevent *bs = b;

	/* Put extra events last */
	if (!!as->desc != !!bs->desc)
		return !!as->desc - !!bs->desc;
	if (as->topic && bs->topic) {
		int n = strcmp(as->topic, bs->topic);

		if (n)
			return n;
	}
	return strcmp(as->name, bs->name);
}

static void wordwrap(char *s, int start, int max, int corr)
{
	int column = start;
	int n;

	while (*s) {
		int wlen = strcspn(s, " \t");

		if (column + wlen >= max && column > start) {
			printf("\n%*s", start, "");
			column = start + corr;
		}
		n = printf("%s%.*s", column > start ? " " : "", wlen, s);
		if (n <= 0)
			break;
		s += wlen;
		column += n;
		while (isspace(*s))
			s++;
	}
}

void print_pmu_events(const char *event_glob, bool name_only, bool quiet_flag,
			bool long_desc)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;
	char buf[1024];
	int printed = 0;
	int len, j;
	struct sevent *aliases;
	int numdesc = 0;
	int columns = pager_get_columns();
	char *topic = NULL;

	pmu = NULL;
	len = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		list_for_each_entry(alias, &pmu->aliases, list)
			len++;
		if (pmu->selectable)
			len++;
	}
	aliases = zalloc(sizeof(struct sevent) * len);
	if (!aliases)
		goto out_enomem;
	pmu = NULL;
	j = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		list_for_each_entry(alias, &pmu->aliases, list) {
			char *name = alias->desc ? alias->name :
				format_alias(buf, sizeof(buf), pmu, alias);
			bool is_cpu = !strcmp(pmu->name, "cpu");

			if (event_glob != NULL &&
			    !(strglobmatch(name, event_glob) ||
			      (!is_cpu && strglobmatch(alias->name,
						       event_glob))))
				continue;

			if (is_cpu && !name_only && !alias->desc)
				name = format_alias_or(buf, sizeof(buf), pmu, alias);

			aliases[j].name = name;
			if (is_cpu && !name_only && !alias->desc)
				aliases[j].name = format_alias_or(buf,
								  sizeof(buf),
								  pmu, alias);
			aliases[j].name = strdup(aliases[j].name);
			if (!aliases[j].name)
				goto out_enomem;

			aliases[j].desc = long_desc ? alias->long_desc :
						alias->desc;
			aliases[j].topic = alias->topic;
			j++;
		}
		if (pmu->selectable &&
		    (event_glob == NULL || strglobmatch(pmu->name, event_glob))) {
			char *s;
			if (asprintf(&s, "%s//", pmu->name) < 0)
				goto out_enomem;
			aliases[j].name = s;
			j++;
		}
	}
	len = j;
	qsort(aliases, len, sizeof(struct sevent), cmp_sevent);
	for (j = 0; j < len; j++) {
		if (name_only) {
			printf("%s ", aliases[j].name);
			continue;
		}
		if (aliases[j].desc && !quiet_flag) {
			if (numdesc++ == 0)
				printf("\n");
			if (aliases[j].topic && (!topic ||
					strcmp(topic, aliases[j].topic))) {
				printf("%s%s:\n", topic ? "\n" : "",
						aliases[j].topic);
				topic = aliases[j].topic;
			}
			printf("  %-50s\n", aliases[j].name);
			printf("%*s", 8, "[");
			wordwrap(aliases[j].desc, 8, columns, 0);
			printf("]\n");
		} else
			printf("  %-50s [Kernel PMU event]\n", aliases[j].name);
		printed++;
	}
	if (printed && pager_in_use())
		printf("\n");
out_free:
	for (j = 0; j < len; j++)
		zfree(&aliases[j].name);
	zfree(&aliases);
	return;

out_enomem:
	printf("FATAL: not enough memory to print PMU events\n");
	if (aliases)
		goto out_free;
}

bool pmu_have_event(const char *pname, const char *name)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;

	pmu = NULL;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (strcmp(pname, pmu->name))
			continue;
		list_for_each_entry(alias, &pmu->aliases, list)
			if (!strcmp(alias->name, name))
				return true;
	}
	return false;
}

static FILE *perf_pmu__open_file(struct perf_pmu *pmu, const char *name)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs;

	sysfs = sysfs__mountpoint();
	if (!sysfs)
		return NULL;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/%s", sysfs, pmu->name, name);

	if (stat(path, &st) < 0)
		return NULL;

	return fopen(path, "r");
}

int perf_pmu__scan_file(struct perf_pmu *pmu, const char *name, const char *fmt,
			...)
{
	va_list args;
	FILE *file;
	int ret = EOF;

	va_start(args, fmt);
	file = perf_pmu__open_file(pmu, name);
	if (file) {
		ret = vfscanf(file, fmt, args);
		fclose(file);
	}
	va_end(args);
	return ret;
}

/*
 * builtin-record.c
 *
 * Builtin record command: Record the profile of a workload
 * (or a CPU, or a PID) into the perf.data output file - for
 * later analysis via perf report.
 */
#include "builtin.h"

#include "perf.h"

#include "util/build-id.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include "util/header.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/debug.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/symbol.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/data.h"

#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

#ifndef HAVE_ON_EXIT_SUPPORT
#ifndef ATEXIT_MAX
#define ATEXIT_MAX 32
#endif
static int __on_exit_count = 0;
typedef void (*on_exit_func_t) (int, void *);
static on_exit_func_t __on_exit_funcs[ATEXIT_MAX];
static void *__on_exit_args[ATEXIT_MAX];
static int __exitcode = 0;
static void __handle_on_exit_funcs(void);
static int on_exit(on_exit_func_t function, void *arg);
#define exit(x) (exit)(__exitcode = (x))

static int on_exit(on_exit_func_t function, void *arg)
{
	if (__on_exit_count == ATEXIT_MAX)
		return -ENOMEM;
	else if (__on_exit_count == 0)
		atexit(__handle_on_exit_funcs);
	__on_exit_funcs[__on_exit_count] = function;
	__on_exit_args[__on_exit_count++] = arg;
	return 0;
}

static void __handle_on_exit_funcs(void)
{
	int i;
	for (i = 0; i < __on_exit_count; i++)
		__on_exit_funcs[i] (__exitcode, __on_exit_args[i]);
}
#endif

struct perf_record {
	struct perf_tool	tool;
	struct perf_record_opts	opts;
	u64			bytes_written;
	struct perf_data_file	file;
	struct perf_evlist	*evlist;
	struct perf_session	*session;
	const char		*progname;
	int			realtime_prio;
	bool			no_buildid;
	bool			no_buildid_cache;
	long			samples;
};

static int do_write_output(struct perf_record *rec, void *buf, size_t size)
{
	struct perf_data_file *file = &rec->file;

	while (size) {
		ssize_t ret = write(file->fd, buf, size);

		if (ret < 0) {
			pr_err("failed to write perf data, error: %m\n");
			return -1;
		}

		size -= ret;
		buf += ret;

		rec->bytes_written += ret;
	}

	return 0;
}

static int write_output(struct perf_record *rec, void *buf, size_t size)
{
	return do_write_output(rec, buf, size);
}

static int process_synthesized_event(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	struct perf_record *rec = container_of(tool, struct perf_record, tool);
	if (write_output(rec, event, event->header.size) < 0)
		return -1;

	return 0;
}

static int perf_record__mmap_read(struct perf_record *rec,
				   struct perf_mmap *md)
{
	unsigned int head = perf_mmap__read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int rc = 0;

	if (old == head)
		return 0;

	rec->samples++;

	size = head - old;

	if ((old & md->mask) + size != (head & md->mask)) {
		buf = &data[old & md->mask];
		size = md->mask + 1 - (old & md->mask);
		old += size;

		if (write_output(rec, buf, size) < 0) {
			rc = -1;
			goto out;
		}
	}

	buf = &data[old & md->mask];
	size = head - old;
	old += size;

	if (write_output(rec, buf, size) < 0) {
		rc = -1;
		goto out;
	}

	md->prev = old;
	perf_mmap__write_tail(md, old);

out:
	return rc;
}

static volatile int done = 0;
static volatile int signr = -1;
static volatile int child_finished = 0;

static void sig_handler(int sig)
{
	if (sig == SIGCHLD)
		child_finished = 1;

	done = 1;
	signr = sig;
}

static void perf_record__sig_exit(int exit_status __maybe_unused, void *arg)
{
	struct perf_record *rec = arg;
	int status;

	if (rec->evlist->workload.pid > 0) {
		if (!child_finished)
			kill(rec->evlist->workload.pid, SIGTERM);

		wait(&status);
		if (WIFSIGNALED(status))
			psignal(WTERMSIG(status), rec->progname);
	}

	if (signr == -1 || signr == SIGUSR1)
		return;

	signal(signr, SIG_DFL);
}

static int perf_record__open(struct perf_record *rec)
{
	char msg[512];
	struct perf_evsel *pos;
	struct perf_evlist *evlist = rec->evlist;
	struct perf_session *session = rec->session;
	struct perf_record_opts *opts = &rec->opts;
	int rc = 0;

	perf_evlist__config(evlist, opts);

	list_for_each_entry(pos, &evlist->entries, node) {
try_again:
		if (perf_evsel__open(pos, evlist->cpus, evlist->threads) < 0) {
			if (perf_evsel__fallback(pos, errno, msg, sizeof(msg))) {
				if (verbose)
					ui__warning("%s\n", msg);
				goto try_again;
			}

			rc = -errno;
			perf_evsel__open_strerror(pos, &opts->target,
						  errno, msg, sizeof(msg));
			ui__error("%s\n", msg);
			goto out;
		}
	}

	if (perf_evlist__apply_filters(evlist)) {
		error("failed to set filter with %d (%s)\n", errno,
			strerror(errno));
		rc = -1;
		goto out;
	}

	if (perf_evlist__mmap(evlist, opts->mmap_pages, false) < 0) {
		if (errno == EPERM) {
			pr_err("Permission error mapping pages.\n"
			       "Consider increasing "
			       "/proc/sys/kernel/perf_event_mlock_kb,\n"
			       "or try again with a smaller value of -m/--mmap_pages.\n"
			       "(current value: %d)\n", opts->mmap_pages);
			rc = -errno;
		} else {
			pr_err("failed to mmap with %d (%s)\n", errno, strerror(errno));
			rc = -errno;
		}
		goto out;
	}

	session->evlist = evlist;
	perf_session__set_id_hdr_size(session);
out:
	return rc;
}

static int process_buildids(struct perf_record *rec)
{
	struct perf_data_file *file  = &rec->file;
	struct perf_session *session = rec->session;
	u64 start = session->header.data_offset;

	u64 size = lseek(file->fd, 0, SEEK_CUR);
	if (size == 0)
		return 0;

	return __perf_session__process_events(session, start,
					      size - start,
					      size, &build_id__mark_dso_hit_ops);
}

static void perf_record__exit(int status, void *arg)
{
	struct perf_record *rec = arg;
	struct perf_data_file *file = &rec->file;

	if (status != 0)
		return;

	if (!file->is_pipe) {
		rec->session->header.data_size += rec->bytes_written;

		if (!rec->no_buildid)
			process_buildids(rec);
		perf_session__write_header(rec->session, rec->evlist,
					   file->fd, true);
		perf_session__delete(rec->session);
		perf_evlist__delete(rec->evlist);
		symbol__exit();
	}
}

static void perf_event__synthesize_guest_os(struct machine *machine, void *data)
{
	int err;
	struct perf_tool *tool = data;
	/*
	 *As for guest kernel when processing subcommand record&report,
	 *we arrange module mmap prior to guest kernel mmap and trigger
	 *a preload dso because default guest module symbols are loaded
	 *from guest kallsyms instead of /lib/modules/XXX/XXX. This
	 *method is used to avoid symbol missing when the first addr is
	 *in module instead of in guest kernel.
	 */
	err = perf_event__synthesize_modules(tool, process_synthesized_event,
					     machine);
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);

	/*
	 * We use _stext for guest kernel because guest kernel's /proc/kallsyms
	 * have no _text sometimes.
	 */
	err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
						 machine, "_text");
	if (err < 0)
		err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
							 machine, "_stext");
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);
}

static struct perf_event_header finished_round_event = {
	.size = sizeof(struct perf_event_header),
	.type = PERF_RECORD_FINISHED_ROUND,
};

static int perf_record__mmap_read_all(struct perf_record *rec)
{
	int i;
	int rc = 0;

	for (i = 0; i < rec->evlist->nr_mmaps; i++) {
		if (rec->evlist->mmap[i].base) {
			if (perf_record__mmap_read(rec, &rec->evlist->mmap[i]) != 0) {
				rc = -1;
				goto out;
			}
		}
	}

	if (perf_header__has_feat(&rec->session->header, HEADER_TRACING_DATA))
		rc = write_output(rec, &finished_round_event,
				  sizeof(finished_round_event));

out:
	return rc;
}

static void perf_record__init_features(struct perf_record *rec)
{
	struct perf_evlist *evsel_list = rec->evlist;
	struct perf_session *session = rec->session;
	int feat;

	for (feat = HEADER_FIRST_FEATURE; feat < HEADER_LAST_FEATURE; feat++)
		perf_header__set_feat(&session->header, feat);

	if (rec->no_buildid)
		perf_header__clear_feat(&session->header, HEADER_BUILD_ID);

	if (!have_tracepoints(&evsel_list->entries))
		perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);

	if (!rec->opts.branch_stack)
		perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);
}

static int __cmd_record(struct perf_record *rec, int argc, const char **argv)
{
	int err;
	unsigned long waking = 0;
	const bool forks = argc > 0;
	struct machine *machine;
	struct perf_tool *tool = &rec->tool;
	struct perf_record_opts *opts = &rec->opts;
	struct perf_evlist *evsel_list = rec->evlist;
	struct perf_data_file *file = &rec->file;
	struct perf_session *session;
	bool disabled = false;

	rec->progname = argv[0];

	on_exit(perf_record__sig_exit, rec);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGTERM, sig_handler);

	session = perf_session__new(file, false, NULL);
	if (session == NULL) {
		pr_err("Not enough memory for reading perf file header\n");
		return -1;
	}

	rec->session = session;

	perf_record__init_features(rec);

	if (forks) {
		err = perf_evlist__prepare_workload(evsel_list, &opts->target,
						    argv, file->is_pipe,
						    true);
		if (err < 0) {
			pr_err("Couldn't run the workload!\n");
			goto out_delete_session;
		}
	}

	if (perf_record__open(rec) != 0) {
		err = -1;
		goto out_delete_session;
	}

	if (!evsel_list->nr_groups)
		perf_header__clear_feat(&session->header, HEADER_GROUP_DESC);

	/*
	 * perf_session__delete(session) will be called at perf_record__exit()
	 */
	on_exit(perf_record__exit, rec);

	if (file->is_pipe) {
		err = perf_header__write_pipe(file->fd);
		if (err < 0)
			goto out_delete_session;
	} else {
		err = perf_session__write_header(session, evsel_list,
						 file->fd, false);
		if (err < 0)
			goto out_delete_session;
	}

	if (!rec->no_buildid
	    && !perf_header__has_feat(&session->header, HEADER_BUILD_ID)) {
		pr_err("Couldn't generate buildids. "
		       "Use --no-buildid to profile anyway.\n");
		err = -1;
		goto out_delete_session;
	}

	machine = &session->machines.host;

	if (file->is_pipe) {
		err = perf_event__synthesize_attrs(tool, session,
						   process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			goto out_delete_session;
		}

		if (have_tracepoints(&evsel_list->entries)) {
			/*
			 * FIXME err <= 0 here actually means that
			 * there were no tracepoints so its not really
			 * an error, just that we don't need to
			 * synthesize anything.  We really have to
			 * return this more properly and also
			 * propagate errors that now are calling die()
			 */
			err = perf_event__synthesize_tracing_data(tool, file->fd, evsel_list,
								  process_synthesized_event);
			if (err <= 0) {
				pr_err("Couldn't record tracing data.\n");
				goto out_delete_session;
			}
			rec->bytes_written += err;
		}
	}

	err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
						 machine, "_text");
	if (err < 0)
		err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
							 machine, "_stext");
	if (err < 0)
		pr_err("Couldn't record kernel reference relocation symbol\n"
		       "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
		       "Check /proc/kallsyms permission or run as root.\n");

	err = perf_event__synthesize_modules(tool, process_synthesized_event,
					     machine);
	if (err < 0)
		pr_err("Couldn't record kernel module information.\n"
		       "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
		       "Check /proc/modules permission or run as root.\n");

	if (perf_guest) {
		machines__process_guests(&session->machines,
					 perf_event__synthesize_guest_os, tool);
	}

	err = __machine__synthesize_threads(machine, tool, &opts->target, evsel_list->threads,
					    process_synthesized_event, opts->sample_address);
	if (err != 0)
		goto out_delete_session;

	if (rec->realtime_prio) {
		struct sched_param param;

		param.sched_priority = rec->realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			pr_err("Could not set realtime priority.\n");
			err = -1;
			goto out_delete_session;
		}
	}

	/*
	 * When perf is starting the traced process, all the events
	 * (apart from group members) have enable_on_exec=1 set,
	 * so don't spoil it by prematurely enabling them.
	 */
	if (!target__none(&opts->target))
		perf_evlist__enable(evsel_list);

	/*
	 * Let the child rip
	 */
	if (forks)
		perf_evlist__start_workload(evsel_list);

	for (;;) {
		int hits = rec->samples;

		if (perf_record__mmap_read_all(rec) < 0) {
			err = -1;
			goto out_delete_session;
		}

		if (hits == rec->samples) {
			if (done)
				break;
			err = poll(evsel_list->pollfd, evsel_list->nr_fds, -1);
			waking++;
		}

		/*
		 * When perf is starting the traced process, at the end events
		 * die with the process and we wait for that. Thus no need to
		 * disable events in this case.
		 */
		if (done && !disabled && !target__none(&opts->target)) {
			perf_evlist__disable(evsel_list);
			disabled = true;
		}
	}

	if (quiet || signr == SIGUSR1)
		return 0;

	fprintf(stderr, "[ perf record: Woken up %ld times to write data ]\n", waking);

	/*
	 * Approximate RIP event size: 24 bytes.
	 */
	fprintf(stderr,
		"[ perf record: Captured and wrote %.3f MB %s (~%" PRIu64 " samples) ]\n",
		(double)rec->bytes_written / 1024.0 / 1024.0,
		file->path,
		rec->bytes_written / 24);

	return 0;

out_delete_session:
	perf_session__delete(session);
	return err;
}

#define BRANCH_OPT(n, m) \
	{ .name = n, .mode = (m) }

#define BRANCH_END { .name = NULL }

struct branch_mode {
	const char *name;
	int mode;
};

static const struct branch_mode branch_modes[] = {
	BRANCH_OPT("u", PERF_SAMPLE_BRANCH_USER),
	BRANCH_OPT("k", PERF_SAMPLE_BRANCH_KERNEL),
	BRANCH_OPT("hv", PERF_SAMPLE_BRANCH_HV),
	BRANCH_OPT("any", PERF_SAMPLE_BRANCH_ANY),
	BRANCH_OPT("any_call", PERF_SAMPLE_BRANCH_ANY_CALL),
	BRANCH_OPT("any_ret", PERF_SAMPLE_BRANCH_ANY_RETURN),
	BRANCH_OPT("ind_call", PERF_SAMPLE_BRANCH_IND_CALL),
	BRANCH_OPT("abort_tx", PERF_SAMPLE_BRANCH_ABORT_TX),
	BRANCH_OPT("in_tx", PERF_SAMPLE_BRANCH_IN_TX),
	BRANCH_OPT("no_tx", PERF_SAMPLE_BRANCH_NO_TX),
	BRANCH_END
};

static int
parse_branch_stack(const struct option *opt, const char *str, int unset)
{
#define ONLY_PLM \
	(PERF_SAMPLE_BRANCH_USER	|\
	 PERF_SAMPLE_BRANCH_KERNEL	|\
	 PERF_SAMPLE_BRANCH_HV)

	uint64_t *mode = (uint64_t *)opt->value;
	const struct branch_mode *br;
	char *s, *os = NULL, *p;
	int ret = -1;

	if (unset)
		return 0;

	/*
	 * cannot set it twice, -b + --branch-filter for instance
	 */
	if (*mode)
		return -1;

	/* str may be NULL in case no arg is passed to -b */
	if (str) {
		/* because str is read-only */
		s = os = strdup(str);
		if (!s)
			return -1;

		for (;;) {
			p = strchr(s, ',');
			if (p)
				*p = '\0';

			for (br = branch_modes; br->name; br++) {
				if (!strcasecmp(s, br->name))
					break;
			}
			if (!br->name) {
				ui__warning("unknown branch filter %s,"
					    " check man page\n", s);
				goto error;
			}

			*mode |= br->mode;

			if (!p)
				break;

			s = p + 1;
		}
	}
	ret = 0;

	/* default to any branch */
	if ((*mode & ~ONLY_PLM) == 0) {
		*mode = PERF_SAMPLE_BRANCH_ANY;
	}
error:
	free(os);
	return ret;
}

#ifdef HAVE_LIBUNWIND_SUPPORT
static int get_stack_size(char *str, unsigned long *_size)
{
	char *endptr;
	unsigned long size;
	unsigned long max_size = round_down(USHRT_MAX, sizeof(u64));

	size = strtoul(str, &endptr, 0);

	do {
		if (*endptr)
			break;

		size = round_up(size, sizeof(u64));
		if (!size || size > max_size)
			break;

		*_size = size;
		return 0;

	} while (0);

	pr_err("callchain: Incorrect stack dump size (max %ld): %s\n",
	       max_size, str);
	return -1;
}
#endif /* HAVE_LIBUNWIND_SUPPORT */

int record_parse_callchain(const char *arg, struct perf_record_opts *opts)
{
	char *tok, *name, *saveptr = NULL;
	char *buf;
	int ret = -1;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(arg) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, arg);

	tok = strtok_r((char *)buf, ",", &saveptr);
	name = tok ? : (char *)buf;

	do {
		/* Framepointer style */
		if (!strncmp(name, "fp", sizeof("fp"))) {
			if (!strtok_r(NULL, ",", &saveptr)) {
				opts->call_graph = CALLCHAIN_FP;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
				       "needed for -g fp\n");
			break;

#ifdef HAVE_LIBUNWIND_SUPPORT
		/* Dwarf style */
		} else if (!strncmp(name, "dwarf", sizeof("dwarf"))) {
			const unsigned long default_stack_dump_size = 8192;

			ret = 0;
			opts->call_graph = CALLCHAIN_DWARF;
			opts->stack_dump_size = default_stack_dump_size;

			tok = strtok_r(NULL, ",", &saveptr);
			if (tok) {
				unsigned long size = 0;

				ret = get_stack_size(tok, &size);
				opts->stack_dump_size = size;
			}
#endif /* HAVE_LIBUNWIND_SUPPORT */
		} else {
			pr_err("callchain: Unknown --call-graph option "
			       "value: %s\n", arg);
			break;
		}

	} while (0);

	free(buf);
	return ret;
}

static void callchain_debug(struct perf_record_opts *opts)
{
	pr_debug("callchain: type %d\n", opts->call_graph);

	if (opts->call_graph == CALLCHAIN_DWARF)
		pr_debug("callchain: stack dump size %d\n",
			 opts->stack_dump_size);
}

int record_parse_callchain_opt(const struct option *opt,
			       const char *arg,
			       int unset)
{
	struct perf_record_opts *opts = opt->value;
	int ret;

	/* --no-call-graph */
	if (unset) {
		opts->call_graph = CALLCHAIN_NONE;
		pr_debug("callchain: disabled\n");
		return 0;
	}

	ret = record_parse_callchain(arg, opts);
	if (!ret)
		callchain_debug(opts);

	return ret;
}

int record_callchain_opt(const struct option *opt,
			 const char *arg __maybe_unused,
			 int unset __maybe_unused)
{
	struct perf_record_opts *opts = opt->value;

	if (opts->call_graph == CALLCHAIN_NONE)
		opts->call_graph = CALLCHAIN_FP;

	callchain_debug(opts);
	return 0;
}

static const char * const record_usage[] = {
	"perf record [<options>] [<command>]",
	"perf record [<options>] -- <command> [<options>]",
	NULL
};

/*
 * XXX Ideally would be local to cmd_record() and passed to a perf_record__new
 * because we need to have access to it in perf_record__exit, that is called
 * after cmd_record() exits, but since record_options need to be accessible to
 * builtin-script, leave it here.
 *
 * At least we don't ouch it in all the other functions here directly.
 *
 * Just say no to tons of global variables, sigh.
 */
static struct perf_record record = {
	.opts = {
		.mmap_pages	     = UINT_MAX,
		.user_freq	     = UINT_MAX,
		.user_interval	     = ULLONG_MAX,
		.freq		     = 4000,
		.target		     = {
			.uses_mmap   = true,
		},
	},
};

#define CALLCHAIN_HELP "setup and enables call-graph (stack chain/backtrace) recording: "

#ifdef HAVE_LIBUNWIND_SUPPORT
const char record_callchain_help[] = CALLCHAIN_HELP "fp dwarf";
#else
const char record_callchain_help[] = CALLCHAIN_HELP "fp";
#endif

/*
 * XXX Will stay a global variable till we fix builtin-script.c to stop messing
 * with it and switch to use the library functions in perf_evlist that came
 * from builtin-record.c, i.e. use perf_record_opts,
 * perf_evlist__prepare_workload, etc instead of fork+exec'in 'perf record',
 * using pipes, etc.
 */
const struct option record_options[] = {
	OPT_CALLBACK('e', "event", &record.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_CALLBACK(0, "filter", &record.evlist, "filter",
		     "event filter", parse_filter),
	OPT_STRING('p', "pid", &record.opts.target.pid, "pid",
		    "record events on existing process id"),
	OPT_STRING('t', "tid", &record.opts.target.tid, "tid",
		    "record events on existing thread id"),
	OPT_INTEGER('r', "realtime", &record.realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_BOOLEAN('D', "no-delay", &record.opts.no_delay,
		    "collect data without buffering"),
	OPT_BOOLEAN('R', "raw-samples", &record.opts.raw_samples,
		    "collect raw sample records from all opened counters"),
	OPT_BOOLEAN('a', "all-cpus", &record.opts.target.system_wide,
			    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &record.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_U64('c', "count", &record.opts.user_interval, "event period to sample"),
	OPT_STRING('o', "output", &record.file.path, "file",
		    "output file name"),
	OPT_BOOLEAN('i', "no-inherit", &record.opts.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_UINTEGER('F', "freq", &record.opts.user_freq, "profile at this frequency"),
	OPT_CALLBACK('m', "mmap-pages", &record.opts.mmap_pages, "pages",
		     "number of mmap data pages",
		     perf_evlist__parse_mmap_pages),
	OPT_BOOLEAN(0, "group", &record.opts.group,
		    "put the counters into a counter group"),
	OPT_CALLBACK_NOOPT('g', NULL, &record.opts,
			   NULL, "enables call-graph recording" ,
			   &record_callchain_opt),
	OPT_CALLBACK(0, "call-graph", &record.opts,
		     "mode[,dump_size]", record_callchain_help,
		     &record_parse_callchain_opt),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('q', "quiet", &quiet, "don't print any message"),
	OPT_BOOLEAN('s', "stat", &record.opts.inherit_stat,
		    "per thread counts"),
	OPT_BOOLEAN('d', "data", &record.opts.sample_address,
		    "Sample addresses"),
	OPT_BOOLEAN('T', "timestamp", &record.opts.sample_time, "Sample timestamps"),
	OPT_BOOLEAN('P', "period", &record.opts.period, "Sample period"),
	OPT_BOOLEAN('n', "no-samples", &record.opts.no_samples,
		    "don't sample"),
	OPT_BOOLEAN('N', "no-buildid-cache", &record.no_buildid_cache,
		    "do not update the buildid cache"),
	OPT_BOOLEAN('B', "no-buildid", &record.no_buildid,
		    "do not collect buildids in perf.data"),
	OPT_CALLBACK('G', "cgroup", &record.evlist, "name",
		     "monitor event in cgroup name only",
		     parse_cgroups),
	OPT_STRING('u', "uid", &record.opts.target.uid_str, "user",
		   "user to profile"),

	OPT_CALLBACK_NOOPT('b', "branch-any", &record.opts.branch_stack,
		     "branch any", "sample any taken branches",
		     parse_branch_stack),

	OPT_CALLBACK('j', "branch-filter", &record.opts.branch_stack,
		     "branch filter mask", "branch stack filter modes",
		     parse_branch_stack),
	OPT_BOOLEAN('W', "weight", &record.opts.sample_weight,
		    "sample by weight (on special events only)"),
	OPT_BOOLEAN(0, "transaction", &record.opts.sample_transaction,
		    "sample transaction flags (special events only)"),
	OPT_BOOLEAN(0, "force-per-cpu", &record.opts.target.force_per_cpu,
		    "force the use of per-cpu mmaps"),
	OPT_END()
};

int cmd_record(int argc, const char **argv, const char *prefix __maybe_unused)
{
	int err = -ENOMEM;
	struct perf_evlist *evsel_list;
	struct perf_record *rec = &record;
	char errbuf[BUFSIZ];

	evsel_list = perf_evlist__new();
	if (evsel_list == NULL)
		return -ENOMEM;

	rec->evlist = evsel_list;

	argc = parse_options(argc, argv, record_options, record_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && target__none(&rec->opts.target))
		usage_with_options(record_usage, record_options);

	if (nr_cgroups && !rec->opts.target.system_wide) {
		ui__error("cgroup monitoring only available in"
			  " system-wide mode\n");
		usage_with_options(record_usage, record_options);
	}

	symbol__init();

	if (symbol_conf.kptr_restrict)
		pr_warning(
"WARNING: Kernel address maps (/proc/{kallsyms,modules}) are restricted,\n"
"check /proc/sys/kernel/kptr_restrict.\n\n"
"Samples in kernel functions may not be resolved if a suitable vmlinux\n"
"file is not found in the buildid cache or in the vmlinux path.\n\n"
"Samples in kernel modules won't be resolved at all.\n\n"
"If some relocation was applied (e.g. kexec) symbols may be misresolved\n"
"even with a suitable vmlinux or kallsyms file.\n\n");

	if (rec->no_buildid_cache || rec->no_buildid)
		disable_buildid_cache();

	if (evsel_list->nr_entries == 0 &&
	    perf_evlist__add_default(evsel_list) < 0) {
		pr_err("Not enough memory for event selector list\n");
		goto out_symbol_exit;
	}

	err = target__validate(&rec->opts.target);
	if (err) {
		target__strerror(&rec->opts.target, err, errbuf, BUFSIZ);
		ui__warning("%s", errbuf);
	}

	err = target__parse_uid(&rec->opts.target);
	if (err) {
		int saved_errno = errno;

		target__strerror(&rec->opts.target, err, errbuf, BUFSIZ);
		ui__error("%s", errbuf);

		err = -saved_errno;
		goto out_symbol_exit;
	}

	err = -ENOMEM;
	if (perf_evlist__create_maps(evsel_list, &rec->opts.target) < 0)
		usage_with_options(record_usage, record_options);

	if (perf_record_opts__config(&rec->opts)) {
		err = -EINVAL;
		goto out_free_fd;
	}

	err = __cmd_record(&record, argc, argv);

	perf_evlist__munmap(evsel_list);
	perf_evlist__close(evsel_list);
out_free_fd:
	perf_evlist__delete_maps(evsel_list);
out_symbol_exit:
	symbol__exit();
	return err;
}

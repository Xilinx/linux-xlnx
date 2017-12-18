/*
 * builtin-trace.c
 *
 * Builtin 'trace' command:
 *
 * Display a continuously updated trace of any workload, CPU, specific PID,
 * system wide, etc.  Default format is loosely strace like, but any other
 * event may be specified using --event.
 *
 * Copyright (C) 2012, 2013, 2014, 2015 Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Initially based on the 'trace' prototype by Thomas Gleixner:
 *
 * http://lwn.net/Articles/415728/ ("Announcing a new utility: 'trace'")
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <traceevent/event-parse.h>
#include <api/fs/tracing_path.h>
#include "builtin.h"
#include "util/color.h"
#include "util/debug.h"
#include "util/evlist.h"
#include <subcmd/exec-cmd.h>
#include "util/machine.h"
#include "util/session.h"
#include "util/thread.h"
#include <subcmd/parse-options.h>
#include "util/strlist.h"
#include "util/intlist.h"
#include "util/thread_map.h"
#include "util/stat.h"
#include "trace-event.h"
#include "util/parse-events.h"
#include "util/bpf-loader.h"
#include "callchain.h"
#include "syscalltbl.h"
#include "rb_resort.h"

#include <libaudit.h> /* FIXME: Still needed for audit_errno_to_name */
#include <stdlib.h>
#include <linux/err.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <linux/random.h>
#include <linux/stringify.h>
#include <linux/time64.h>

#ifndef O_CLOEXEC
# define O_CLOEXEC		02000000
#endif

struct trace {
	struct perf_tool	tool;
	struct syscalltbl	*sctbl;
	struct {
		int		max;
		struct syscall  *table;
		struct {
			struct perf_evsel *sys_enter,
					  *sys_exit;
		}		events;
	} syscalls;
	struct record_opts	opts;
	struct perf_evlist	*evlist;
	struct machine		*host;
	struct thread		*current;
	u64			base_time;
	FILE			*output;
	unsigned long		nr_events;
	struct strlist		*ev_qualifier;
	struct {
		size_t		nr;
		int		*entries;
	}			ev_qualifier_ids;
	struct intlist		*tid_list;
	struct intlist		*pid_list;
	struct {
		size_t		nr;
		pid_t		*entries;
	}			filter_pids;
	double			duration_filter;
	double			runtime_ms;
	struct {
		u64		vfs_getname,
				proc_getname;
	} stats;
	unsigned int		max_stack;
	unsigned int		min_stack;
	bool			not_ev_qualifier;
	bool			live;
	bool			full_time;
	bool			sched;
	bool			multiple_threads;
	bool			summary;
	bool			summary_only;
	bool			show_comm;
	bool			show_tool_stats;
	bool			trace_syscalls;
	bool			kernel_syscallchains;
	bool			force;
	bool			vfs_getname;
	int			trace_pgfaults;
	int			open_id;
};

struct tp_field {
	int offset;
	union {
		u64 (*integer)(struct tp_field *field, struct perf_sample *sample);
		void *(*pointer)(struct tp_field *field, struct perf_sample *sample);
	};
};

#define TP_UINT_FIELD(bits) \
static u64 tp_field__u##bits(struct tp_field *field, struct perf_sample *sample) \
{ \
	u##bits value; \
	memcpy(&value, sample->raw_data + field->offset, sizeof(value)); \
	return value;  \
}

TP_UINT_FIELD(8);
TP_UINT_FIELD(16);
TP_UINT_FIELD(32);
TP_UINT_FIELD(64);

#define TP_UINT_FIELD__SWAPPED(bits) \
static u64 tp_field__swapped_u##bits(struct tp_field *field, struct perf_sample *sample) \
{ \
	u##bits value; \
	memcpy(&value, sample->raw_data + field->offset, sizeof(value)); \
	return bswap_##bits(value);\
}

TP_UINT_FIELD__SWAPPED(16);
TP_UINT_FIELD__SWAPPED(32);
TP_UINT_FIELD__SWAPPED(64);

static int tp_field__init_uint(struct tp_field *field,
			       struct format_field *format_field,
			       bool needs_swap)
{
	field->offset = format_field->offset;

	switch (format_field->size) {
	case 1:
		field->integer = tp_field__u8;
		break;
	case 2:
		field->integer = needs_swap ? tp_field__swapped_u16 : tp_field__u16;
		break;
	case 4:
		field->integer = needs_swap ? tp_field__swapped_u32 : tp_field__u32;
		break;
	case 8:
		field->integer = needs_swap ? tp_field__swapped_u64 : tp_field__u64;
		break;
	default:
		return -1;
	}

	return 0;
}

static void *tp_field__ptr(struct tp_field *field, struct perf_sample *sample)
{
	return sample->raw_data + field->offset;
}

static int tp_field__init_ptr(struct tp_field *field, struct format_field *format_field)
{
	field->offset = format_field->offset;
	field->pointer = tp_field__ptr;
	return 0;
}

struct syscall_tp {
	struct tp_field id;
	union {
		struct tp_field args, ret;
	};
};

static int perf_evsel__init_tp_uint_field(struct perf_evsel *evsel,
					  struct tp_field *field,
					  const char *name)
{
	struct format_field *format_field = perf_evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_uint(field, format_field, evsel->needs_swap);
}

#define perf_evsel__init_sc_tp_uint_field(evsel, name) \
	({ struct syscall_tp *sc = evsel->priv;\
	   perf_evsel__init_tp_uint_field(evsel, &sc->name, #name); })

static int perf_evsel__init_tp_ptr_field(struct perf_evsel *evsel,
					 struct tp_field *field,
					 const char *name)
{
	struct format_field *format_field = perf_evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_ptr(field, format_field);
}

#define perf_evsel__init_sc_tp_ptr_field(evsel, name) \
	({ struct syscall_tp *sc = evsel->priv;\
	   perf_evsel__init_tp_ptr_field(evsel, &sc->name, #name); })

static void perf_evsel__delete_priv(struct perf_evsel *evsel)
{
	zfree(&evsel->priv);
	perf_evsel__delete(evsel);
}

static int perf_evsel__init_syscall_tp(struct perf_evsel *evsel, void *handler)
{
	evsel->priv = malloc(sizeof(struct syscall_tp));
	if (evsel->priv != NULL) {
		if (perf_evsel__init_sc_tp_uint_field(evsel, id))
			goto out_delete;

		evsel->handler = handler;
		return 0;
	}

	return -ENOMEM;

out_delete:
	zfree(&evsel->priv);
	return -ENOENT;
}

static struct perf_evsel *perf_evsel__syscall_newtp(const char *direction, void *handler)
{
	struct perf_evsel *evsel = perf_evsel__newtp("raw_syscalls", direction);

	/* older kernel (e.g., RHEL6) use syscalls:{enter,exit} */
	if (IS_ERR(evsel))
		evsel = perf_evsel__newtp("syscalls", direction);

	if (IS_ERR(evsel))
		return NULL;

	if (perf_evsel__init_syscall_tp(evsel, handler))
		goto out_delete;

	return evsel;

out_delete:
	perf_evsel__delete_priv(evsel);
	return NULL;
}

#define perf_evsel__sc_tp_uint(evsel, name, sample) \
	({ struct syscall_tp *fields = evsel->priv; \
	   fields->name.integer(&fields->name, sample); })

#define perf_evsel__sc_tp_ptr(evsel, name, sample) \
	({ struct syscall_tp *fields = evsel->priv; \
	   fields->name.pointer(&fields->name, sample); })

struct syscall_arg {
	unsigned long val;
	struct thread *thread;
	struct trace  *trace;
	void	      *parm;
	u8	      idx;
	u8	      mask;
};

struct strarray {
	int	    offset;
	int	    nr_entries;
	const char **entries;
};

#define DEFINE_STRARRAY(array) struct strarray strarray__##array = { \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

#define DEFINE_STRARRAY_OFFSET(array, off) struct strarray strarray__##array = { \
	.offset	    = off, \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

static size_t __syscall_arg__scnprintf_strarray(char *bf, size_t size,
						const char *intfmt,
					        struct syscall_arg *arg)
{
	struct strarray *sa = arg->parm;
	int idx = arg->val - sa->offset;

	if (idx < 0 || idx >= sa->nr_entries)
		return scnprintf(bf, size, intfmt, arg->val);

	return scnprintf(bf, size, "%s", sa->entries[idx]);
}

static size_t syscall_arg__scnprintf_strarray(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	return __syscall_arg__scnprintf_strarray(bf, size, "%d", arg);
}

#define SCA_STRARRAY syscall_arg__scnprintf_strarray

#if defined(__i386__) || defined(__x86_64__)
/*
 * FIXME: Make this available to all arches as soon as the ioctl beautifier
 * 	  gets rewritten to support all arches.
 */
static size_t syscall_arg__scnprintf_strhexarray(char *bf, size_t size,
						 struct syscall_arg *arg)
{
	return __syscall_arg__scnprintf_strarray(bf, size, "%#x", arg);
}

#define SCA_STRHEXARRAY syscall_arg__scnprintf_strhexarray
#endif /* defined(__i386__) || defined(__x86_64__) */

static size_t syscall_arg__scnprintf_fd(char *bf, size_t size,
					struct syscall_arg *arg);

#define SCA_FD syscall_arg__scnprintf_fd

#ifndef AT_FDCWD
#define AT_FDCWD	-100
#endif

static size_t syscall_arg__scnprintf_fd_at(char *bf, size_t size,
					   struct syscall_arg *arg)
{
	int fd = arg->val;

	if (fd == AT_FDCWD)
		return scnprintf(bf, size, "CWD");

	return syscall_arg__scnprintf_fd(bf, size, arg);
}

#define SCA_FDAT syscall_arg__scnprintf_fd_at

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg);

#define SCA_CLOSE_FD syscall_arg__scnprintf_close_fd

static size_t syscall_arg__scnprintf_hex(char *bf, size_t size,
					 struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%#lx", arg->val);
}

#define SCA_HEX syscall_arg__scnprintf_hex

static size_t syscall_arg__scnprintf_int(char *bf, size_t size,
					 struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%d", arg->val);
}

#define SCA_INT syscall_arg__scnprintf_int

static const char *bpf_cmd[] = {
	"MAP_CREATE", "MAP_LOOKUP_ELEM", "MAP_UPDATE_ELEM", "MAP_DELETE_ELEM",
	"MAP_GET_NEXT_KEY", "PROG_LOAD",
};
static DEFINE_STRARRAY(bpf_cmd);

static const char *epoll_ctl_ops[] = { "ADD", "DEL", "MOD", };
static DEFINE_STRARRAY_OFFSET(epoll_ctl_ops, 1);

static const char *itimers[] = { "REAL", "VIRTUAL", "PROF", };
static DEFINE_STRARRAY(itimers);

static const char *keyctl_options[] = {
	"GET_KEYRING_ID", "JOIN_SESSION_KEYRING", "UPDATE", "REVOKE", "CHOWN",
	"SETPERM", "DESCRIBE", "CLEAR", "LINK", "UNLINK", "SEARCH", "READ",
	"INSTANTIATE", "NEGATE", "SET_REQKEY_KEYRING", "SET_TIMEOUT",
	"ASSUME_AUTHORITY", "GET_SECURITY", "SESSION_TO_PARENT", "REJECT",
	"INSTANTIATE_IOV", "INVALIDATE", "GET_PERSISTENT",
};
static DEFINE_STRARRAY(keyctl_options);

static const char *whences[] = { "SET", "CUR", "END",
#ifdef SEEK_DATA
"DATA",
#endif
#ifdef SEEK_HOLE
"HOLE",
#endif
};
static DEFINE_STRARRAY(whences);

static const char *fcntl_cmds[] = {
	"DUPFD", "GETFD", "SETFD", "GETFL", "SETFL", "GETLK", "SETLK",
	"SETLKW", "SETOWN", "GETOWN", "SETSIG", "GETSIG", "F_GETLK64",
	"F_SETLK64", "F_SETLKW64", "F_SETOWN_EX", "F_GETOWN_EX",
	"F_GETOWNER_UIDS",
};
static DEFINE_STRARRAY(fcntl_cmds);

static const char *rlimit_resources[] = {
	"CPU", "FSIZE", "DATA", "STACK", "CORE", "RSS", "NPROC", "NOFILE",
	"MEMLOCK", "AS", "LOCKS", "SIGPENDING", "MSGQUEUE", "NICE", "RTPRIO",
	"RTTIME",
};
static DEFINE_STRARRAY(rlimit_resources);

static const char *sighow[] = { "BLOCK", "UNBLOCK", "SETMASK", };
static DEFINE_STRARRAY(sighow);

static const char *clockid[] = {
	"REALTIME", "MONOTONIC", "PROCESS_CPUTIME_ID", "THREAD_CPUTIME_ID",
	"MONOTONIC_RAW", "REALTIME_COARSE", "MONOTONIC_COARSE", "BOOTTIME",
	"REALTIME_ALARM", "BOOTTIME_ALARM", "SGI_CYCLE", "TAI"
};
static DEFINE_STRARRAY(clockid);

static const char *socket_families[] = {
	"UNSPEC", "LOCAL", "INET", "AX25", "IPX", "APPLETALK", "NETROM",
	"BRIDGE", "ATMPVC", "X25", "INET6", "ROSE", "DECnet", "NETBEUI",
	"SECURITY", "KEY", "NETLINK", "PACKET", "ASH", "ECONET", "ATMSVC",
	"RDS", "SNA", "IRDA", "PPPOX", "WANPIPE", "LLC", "IB", "CAN", "TIPC",
	"BLUETOOTH", "IUCV", "RXRPC", "ISDN", "PHONET", "IEEE802154", "CAIF",
	"ALG", "NFC", "VSOCK",
};
static DEFINE_STRARRAY(socket_families);

static size_t syscall_arg__scnprintf_access_mode(char *bf, size_t size,
						 struct syscall_arg *arg)
{
	size_t printed = 0;
	int mode = arg->val;

	if (mode == F_OK) /* 0 */
		return scnprintf(bf, size, "F");
#define	P_MODE(n) \
	if (mode & n##_OK) { \
		printed += scnprintf(bf + printed, size - printed, "%s", #n); \
		mode &= ~n##_OK; \
	}

	P_MODE(R);
	P_MODE(W);
	P_MODE(X);
#undef P_MODE

	if (mode)
		printed += scnprintf(bf + printed, size - printed, "|%#x", mode);

	return printed;
}

#define SCA_ACCMODE syscall_arg__scnprintf_access_mode

static size_t syscall_arg__scnprintf_filename(char *bf, size_t size,
					      struct syscall_arg *arg);

#define SCA_FILENAME syscall_arg__scnprintf_filename

static size_t syscall_arg__scnprintf_pipe_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & O_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~O_##n; \
	}

	P_FLAG(CLOEXEC);
	P_FLAG(NONBLOCK);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_PIPE_FLAGS syscall_arg__scnprintf_pipe_flags

#if defined(__i386__) || defined(__x86_64__)
/*
 * FIXME: Make this available to all arches.
 */
#define TCGETS		0x5401

static const char *tioctls[] = {
	"TCGETS", "TCSETS", "TCSETSW", "TCSETSF", "TCGETA", "TCSETA", "TCSETAW",
	"TCSETAF", "TCSBRK", "TCXONC", "TCFLSH", "TIOCEXCL", "TIOCNXCL",
	"TIOCSCTTY", "TIOCGPGRP", "TIOCSPGRP", "TIOCOUTQ", "TIOCSTI",
	"TIOCGWINSZ", "TIOCSWINSZ", "TIOCMGET", "TIOCMBIS", "TIOCMBIC",
	"TIOCMSET", "TIOCGSOFTCAR", "TIOCSSOFTCAR", "FIONREAD", "TIOCLINUX",
	"TIOCCONS", "TIOCGSERIAL", "TIOCSSERIAL", "TIOCPKT", "FIONBIO",
	"TIOCNOTTY", "TIOCSETD", "TIOCGETD", "TCSBRKP", [0x27] = "TIOCSBRK",
	"TIOCCBRK", "TIOCGSID", "TCGETS2", "TCSETS2", "TCSETSW2", "TCSETSF2",
	"TIOCGRS485", "TIOCSRS485", "TIOCGPTN", "TIOCSPTLCK",
	"TIOCGDEV||TCGETX", "TCSETX", "TCSETXF", "TCSETXW", "TIOCSIG",
	"TIOCVHANGUP", "TIOCGPKT", "TIOCGPTLCK", "TIOCGEXCL",
	[0x50] = "FIONCLEX", "FIOCLEX", "FIOASYNC", "TIOCSERCONFIG",
	"TIOCSERGWILD", "TIOCSERSWILD", "TIOCGLCKTRMIOS", "TIOCSLCKTRMIOS",
	"TIOCSERGSTRUCT", "TIOCSERGETLSR", "TIOCSERGETMULTI", "TIOCSERSETMULTI",
	"TIOCMIWAIT", "TIOCGICOUNT", [0x60] = "FIOQSIZE",
};

static DEFINE_STRARRAY_OFFSET(tioctls, 0x5401);
#endif /* defined(__i386__) || defined(__x86_64__) */

#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK	0x0001
#endif
#ifndef GRND_RANDOM
#define GRND_RANDOM	0x0002
#endif

static size_t syscall_arg__scnprintf_getrandom_flags(char *bf, size_t size,
						   struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & GRND_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~GRND_##n; \
	}

	P_FLAG(RANDOM);
	P_FLAG(NONBLOCK);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_GETRANDOM_FLAGS syscall_arg__scnprintf_getrandom_flags

#define STRARRAY(arg, name, array) \
	  .arg_scnprintf = { [arg] = SCA_STRARRAY, }, \
	  .arg_parm	 = { [arg] = &strarray__##array, }

#include "trace/beauty/eventfd.c"
#include "trace/beauty/flock.c"
#include "trace/beauty/futex_op.c"
#include "trace/beauty/mmap.c"
#include "trace/beauty/mode_t.c"
#include "trace/beauty/msg_flags.c"
#include "trace/beauty/open_flags.c"
#include "trace/beauty/perf_event_open.c"
#include "trace/beauty/pid.c"
#include "trace/beauty/sched_policy.c"
#include "trace/beauty/seccomp.c"
#include "trace/beauty/signum.c"
#include "trace/beauty/socket_type.c"
#include "trace/beauty/waitid_options.c"

static struct syscall_fmt {
	const char *name;
	const char *alias;
	size_t	   (*arg_scnprintf[6])(char *bf, size_t size, struct syscall_arg *arg);
	void	   *arg_parm[6];
	bool	   errmsg;
	bool	   errpid;
	bool	   timeout;
	bool	   hexret;
} syscall_fmts[] = {
	{ .name	    = "access",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_ACCMODE,  /* mode */ }, },
	{ .name	    = "arch_prctl", .errmsg = true, .alias = "prctl", },
	{ .name	    = "bpf",	    .errmsg = true, STRARRAY(0, cmd, bpf_cmd), },
	{ .name	    = "brk",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* brk */ }, },
	{ .name	    = "chdir",	    .errmsg = true, },
	{ .name	    = "chmod",	    .errmsg = true, },
	{ .name	    = "chroot",	    .errmsg = true, },
	{ .name     = "clock_gettime",  .errmsg = true, STRARRAY(0, clk_id, clockid), },
	{ .name	    = "clone",	    .errpid = true, },
	{ .name	    = "close",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_CLOSE_FD, /* fd */ }, },
	{ .name	    = "connect",    .errmsg = true, },
	{ .name	    = "creat",	    .errmsg = true, },
	{ .name	    = "dup",	    .errmsg = true, },
	{ .name	    = "dup2",	    .errmsg = true, },
	{ .name	    = "dup3",	    .errmsg = true, },
	{ .name	    = "epoll_ctl",  .errmsg = true, STRARRAY(1, op, epoll_ctl_ops), },
	{ .name	    = "eventfd2",   .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_EFD_FLAGS, /* flags */ }, },
	{ .name	    = "faccessat",  .errmsg = true, },
	{ .name	    = "fadvise64",  .errmsg = true, },
	{ .name	    = "fallocate",  .errmsg = true, },
	{ .name	    = "fchdir",	    .errmsg = true, },
	{ .name	    = "fchmod",	    .errmsg = true, },
	{ .name	    = "fchmodat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "fchown",	    .errmsg = true, },
	{ .name	    = "fchownat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "fcntl",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_STRARRAY, /* cmd */ },
	  .arg_parm	 = { [1] = &strarray__fcntl_cmds, /* cmd */ }, },
	{ .name	    = "fdatasync",  .errmsg = true, },
	{ .name	    = "flock",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_FLOCK, /* cmd */ }, },
	{ .name	    = "fsetxattr",  .errmsg = true, },
	{ .name	    = "fstat",	    .errmsg = true, .alias = "newfstat", },
	{ .name	    = "fstatat",    .errmsg = true, .alias = "newfstatat", },
	{ .name	    = "fstatfs",    .errmsg = true, },
	{ .name	    = "fsync",    .errmsg = true, },
	{ .name	    = "ftruncate", .errmsg = true, },
	{ .name	    = "futex",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_FUTEX_OP, /* op */ }, },
	{ .name	    = "futimesat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "getdents",   .errmsg = true, },
	{ .name	    = "getdents64", .errmsg = true, },
	{ .name	    = "getitimer",  .errmsg = true, STRARRAY(0, which, itimers), },
	{ .name	    = "getpid",	    .errpid = true, },
	{ .name	    = "getpgid",    .errpid = true, },
	{ .name	    = "getppid",    .errpid = true, },
	{ .name	    = "getrandom",  .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_GETRANDOM_FLAGS, /* flags */ }, },
	{ .name	    = "getrlimit",  .errmsg = true, STRARRAY(0, resource, rlimit_resources), },
	{ .name	    = "getxattr",   .errmsg = true, },
	{ .name	    = "inotify_add_watch",	    .errmsg = true, },
	{ .name	    = "ioctl",	    .errmsg = true,
	  .arg_scnprintf = {
#if defined(__i386__) || defined(__x86_64__)
/*
 * FIXME: Make this available to all arches.
 */
			     [1] = SCA_STRHEXARRAY, /* cmd */
			     [2] = SCA_HEX, /* arg */ },
	  .arg_parm	 = { [1] = &strarray__tioctls, /* cmd */ }, },
#else
			     [2] = SCA_HEX, /* arg */ }, },
#endif
	{ .name	    = "keyctl",	    .errmsg = true, STRARRAY(0, option, keyctl_options), },
	{ .name	    = "kill",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "lchown",    .errmsg = true, },
	{ .name	    = "lgetxattr",  .errmsg = true, },
	{ .name	    = "linkat",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "listxattr",  .errmsg = true, },
	{ .name	    = "llistxattr", .errmsg = true, },
	{ .name	    = "lremovexattr",  .errmsg = true, },
	{ .name	    = "lseek",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_STRARRAY, /* whence */ },
	  .arg_parm	 = { [2] = &strarray__whences, /* whence */ }, },
	{ .name	    = "lsetxattr",  .errmsg = true, },
	{ .name	    = "lstat",	    .errmsg = true, .alias = "newlstat", },
	{ .name	    = "lsxattr",    .errmsg = true, },
	{ .name     = "madvise",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	 /* start */
			     [2] = SCA_MADV_BHV, /* behavior */ }, },
	{ .name	    = "mkdir",    .errmsg = true, },
	{ .name	    = "mkdirat",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "mknod",      .errmsg = true, },
	{ .name	    = "mknodat",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, },
	{ .name	    = "mlock",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "mlockall",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "mmap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	  /* addr */
			     [2] = SCA_MMAP_PROT, /* prot */
			     [3] = SCA_MMAP_FLAGS, /* flags */ }, },
	{ .name	    = "mprotect",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* start */
			     [2] = SCA_MMAP_PROT, /* prot */ }, },
	{ .name	    = "mq_unlink", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FILENAME, /* u_name */ }, },
	{ .name	    = "mremap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */
			     [3] = SCA_MREMAP_FLAGS, /* flags */
			     [4] = SCA_HEX, /* new_addr */ }, },
	{ .name	    = "munlock",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "munmap",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "name_to_handle_at", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "newfstatat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "open",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "open_by_handle_at", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */
			     [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "openat",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */
			     [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "perf_event_open", .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_INT, /* cpu */
			     [3] = SCA_FD,  /* group_fd */
			     [4] = SCA_PERF_FLAGS,  /* flags */ }, },
	{ .name	    = "pipe2",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_PIPE_FLAGS, /* flags */ }, },
	{ .name	    = "poll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "ppoll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "pread",	    .errmsg = true, .alias = "pread64", },
	{ .name	    = "preadv",	    .errmsg = true, .alias = "pread", },
	{ .name	    = "prlimit64",  .errmsg = true, STRARRAY(1, resource, rlimit_resources), },
	{ .name	    = "pwrite",	    .errmsg = true, .alias = "pwrite64", },
	{ .name	    = "pwritev",    .errmsg = true, },
	{ .name	    = "read",	    .errmsg = true, },
	{ .name	    = "readlink",   .errmsg = true, },
	{ .name	    = "readlinkat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "readv",	    .errmsg = true, },
	{ .name	    = "recvfrom",   .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "recvmmsg",   .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "recvmsg",    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "removexattr", .errmsg = true, },
	{ .name	    = "renameat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "rmdir",    .errmsg = true, },
	{ .name	    = "rt_sigaction", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "rt_sigprocmask",  .errmsg = true, STRARRAY(0, how, sighow), },
	{ .name	    = "rt_sigqueueinfo", .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "rt_tgsigqueueinfo", .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "sched_getattr",	      .errmsg = true, },
	{ .name	    = "sched_setattr",	      .errmsg = true, },
	{ .name	    = "sched_setscheduler",   .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SCHED_POLICY, /* policy */ }, },
	{ .name	    = "seccomp", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_SECCOMP_OP, /* op */
			     [1] = SCA_SECCOMP_FLAGS, /* flags */ }, },
	{ .name	    = "select",	    .errmsg = true, .timeout = true, },
	{ .name	    = "sendmmsg",    .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "sendmsg",    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "sendto",	    .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "set_tid_address", .errpid = true, },
	{ .name	    = "setitimer",  .errmsg = true, STRARRAY(0, which, itimers), },
	{ .name	    = "setpgid",    .errmsg = true, },
	{ .name	    = "setrlimit",  .errmsg = true, STRARRAY(0, resource, rlimit_resources), },
	{ .name	    = "setxattr",   .errmsg = true, },
	{ .name	    = "shutdown",   .errmsg = true, },
	{ .name	    = "socket",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_STRARRAY, /* family */
			     [1] = SCA_SK_TYPE, /* type */ },
	  .arg_parm	 = { [0] = &strarray__socket_families, /* family */ }, },
	{ .name	    = "socketpair", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_STRARRAY, /* family */
			     [1] = SCA_SK_TYPE, /* type */ },
	  .arg_parm	 = { [0] = &strarray__socket_families, /* family */ }, },
	{ .name	    = "stat",	    .errmsg = true, .alias = "newstat", },
	{ .name	    = "statfs",	    .errmsg = true, },
	{ .name	    = "swapoff",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FILENAME, /* specialfile */ }, },
	{ .name	    = "swapon",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FILENAME, /* specialfile */ }, },
	{ .name	    = "symlinkat",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "tgkill",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "tkill",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "truncate",   .errmsg = true, },
	{ .name	    = "uname",	    .errmsg = true, .alias = "newuname", },
	{ .name	    = "unlinkat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "utime",  .errmsg = true, },
	{ .name	    = "utimensat",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dirfd */ }, },
	{ .name	    = "utimes",  .errmsg = true, },
	{ .name	    = "vmsplice",  .errmsg = true, },
	{ .name	    = "wait4",	    .errpid = true,
	  .arg_scnprintf = { [2] = SCA_WAITID_OPTIONS, /* options */ }, },
	{ .name	    = "waitid",	    .errpid = true,
	  .arg_scnprintf = { [3] = SCA_WAITID_OPTIONS, /* options */ }, },
	{ .name	    = "write",	    .errmsg = true, },
	{ .name	    = "writev",	    .errmsg = true, },
};

static int syscall_fmt__cmp(const void *name, const void *fmtp)
{
	const struct syscall_fmt *fmt = fmtp;
	return strcmp(name, fmt->name);
}

static struct syscall_fmt *syscall_fmt__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(syscall_fmts);
	return bsearch(name, syscall_fmts, nmemb, sizeof(struct syscall_fmt), syscall_fmt__cmp);
}

struct syscall {
	struct event_format *tp_format;
	int		    nr_args;
	struct format_field *args;
	const char	    *name;
	bool		    is_exit;
	struct syscall_fmt  *fmt;
	size_t		    (**arg_scnprintf)(char *bf, size_t size, struct syscall_arg *arg);
	void		    **arg_parm;
};

static size_t fprintf_duration(unsigned long t, FILE *fp)
{
	double duration = (double)t / NSEC_PER_MSEC;
	size_t printed = fprintf(fp, "(");

	if (duration >= 1.0)
		printed += color_fprintf(fp, PERF_COLOR_RED, "%6.3f ms", duration);
	else if (duration >= 0.01)
		printed += color_fprintf(fp, PERF_COLOR_YELLOW, "%6.3f ms", duration);
	else
		printed += color_fprintf(fp, PERF_COLOR_NORMAL, "%6.3f ms", duration);
	return printed + fprintf(fp, "): ");
}

/**
 * filename.ptr: The filename char pointer that will be vfs_getname'd
 * filename.entry_str_pos: Where to insert the string translated from
 *                         filename.ptr by the vfs_getname tracepoint/kprobe.
 */
struct thread_trace {
	u64		  entry_time;
	u64		  exit_time;
	bool		  entry_pending;
	unsigned long	  nr_events;
	unsigned long	  pfmaj, pfmin;
	char		  *entry_str;
	double		  runtime_ms;
        struct {
		unsigned long ptr;
		short int     entry_str_pos;
		bool	      pending_open;
		unsigned int  namelen;
		char	      *name;
	} filename;
	struct {
		int	  max;
		char	  **table;
	} paths;

	struct intlist *syscall_stats;
};

static struct thread_trace *thread_trace__new(void)
{
	struct thread_trace *ttrace =  zalloc(sizeof(struct thread_trace));

	if (ttrace)
		ttrace->paths.max = -1;

	ttrace->syscall_stats = intlist__new(NULL);

	return ttrace;
}

static struct thread_trace *thread__trace(struct thread *thread, FILE *fp)
{
	struct thread_trace *ttrace;

	if (thread == NULL)
		goto fail;

	if (thread__priv(thread) == NULL)
		thread__set_priv(thread, thread_trace__new());

	if (thread__priv(thread) == NULL)
		goto fail;

	ttrace = thread__priv(thread);
	++ttrace->nr_events;

	return ttrace;
fail:
	color_fprintf(fp, PERF_COLOR_RED,
		      "WARNING: not enough memory, dropping samples!\n");
	return NULL;
}

#define TRACE_PFMAJ		(1 << 0)
#define TRACE_PFMIN		(1 << 1)

static const size_t trace__entry_str_size = 2048;

static int trace__set_fd_pathname(struct thread *thread, int fd, const char *pathname)
{
	struct thread_trace *ttrace = thread__priv(thread);

	if (fd > ttrace->paths.max) {
		char **npath = realloc(ttrace->paths.table, (fd + 1) * sizeof(char *));

		if (npath == NULL)
			return -1;

		if (ttrace->paths.max != -1) {
			memset(npath + ttrace->paths.max + 1, 0,
			       (fd - ttrace->paths.max) * sizeof(char *));
		} else {
			memset(npath, 0, (fd + 1) * sizeof(char *));
		}

		ttrace->paths.table = npath;
		ttrace->paths.max   = fd;
	}

	ttrace->paths.table[fd] = strdup(pathname);

	return ttrace->paths.table[fd] != NULL ? 0 : -1;
}

static int thread__read_fd_path(struct thread *thread, int fd)
{
	char linkname[PATH_MAX], pathname[PATH_MAX];
	struct stat st;
	int ret;

	if (thread->pid_ == thread->tid) {
		scnprintf(linkname, sizeof(linkname),
			  "/proc/%d/fd/%d", thread->pid_, fd);
	} else {
		scnprintf(linkname, sizeof(linkname),
			  "/proc/%d/task/%d/fd/%d", thread->pid_, thread->tid, fd);
	}

	if (lstat(linkname, &st) < 0 || st.st_size + 1 > (off_t)sizeof(pathname))
		return -1;

	ret = readlink(linkname, pathname, sizeof(pathname));

	if (ret < 0 || ret > st.st_size)
		return -1;

	pathname[ret] = '\0';
	return trace__set_fd_pathname(thread, fd, pathname);
}

static const char *thread__fd_path(struct thread *thread, int fd,
				   struct trace *trace)
{
	struct thread_trace *ttrace = thread__priv(thread);

	if (ttrace == NULL)
		return NULL;

	if (fd < 0)
		return NULL;

	if ((fd > ttrace->paths.max || ttrace->paths.table[fd] == NULL)) {
		if (!trace->live)
			return NULL;
		++trace->stats.proc_getname;
		if (thread__read_fd_path(thread, fd))
			return NULL;
	}

	return ttrace->paths.table[fd];
}

static size_t syscall_arg__scnprintf_fd(char *bf, size_t size,
					struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = scnprintf(bf, size, "%d", fd);
	const char *path = thread__fd_path(arg->thread, fd, arg->trace);

	if (path)
		printed += scnprintf(bf + printed, size - printed, "<%s>", path);

	return printed;
}

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = syscall_arg__scnprintf_fd(bf, size, arg);
	struct thread_trace *ttrace = thread__priv(arg->thread);

	if (ttrace && fd >= 0 && fd <= ttrace->paths.max)
		zfree(&ttrace->paths.table[fd]);

	return printed;
}

static void thread__set_filename_pos(struct thread *thread, const char *bf,
				     unsigned long ptr)
{
	struct thread_trace *ttrace = thread__priv(thread);

	ttrace->filename.ptr = ptr;
	ttrace->filename.entry_str_pos = bf - ttrace->entry_str;
}

static size_t syscall_arg__scnprintf_filename(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	unsigned long ptr = arg->val;

	if (!arg->trace->vfs_getname)
		return scnprintf(bf, size, "%#x", ptr);

	thread__set_filename_pos(arg->thread, bf, ptr);
	return 0;
}

static bool trace__filter_duration(struct trace *trace, double t)
{
	return t < (trace->duration_filter * NSEC_PER_MSEC);
}

static size_t trace__fprintf_tstamp(struct trace *trace, u64 tstamp, FILE *fp)
{
	double ts = (double)(tstamp - trace->base_time) / NSEC_PER_MSEC;

	return fprintf(fp, "%10.3f ", ts);
}

static bool done = false;
static bool interrupted = false;

static void sig_handler(int sig)
{
	done = true;
	interrupted = sig == SIGINT;
}

static size_t trace__fprintf_entry_head(struct trace *trace, struct thread *thread,
					u64 duration, u64 tstamp, FILE *fp)
{
	size_t printed = trace__fprintf_tstamp(trace, tstamp, fp);
	printed += fprintf_duration(duration, fp);

	if (trace->multiple_threads) {
		if (trace->show_comm)
			printed += fprintf(fp, "%.14s/", thread__comm_str(thread));
		printed += fprintf(fp, "%d ", thread->tid);
	}

	return printed;
}

static int trace__process_event(struct trace *trace, struct machine *machine,
				union perf_event *event, struct perf_sample *sample)
{
	int ret = 0;

	switch (event->header.type) {
	case PERF_RECORD_LOST:
		color_fprintf(trace->output, PERF_COLOR_RED,
			      "LOST %" PRIu64 " events!\n", event->lost.lost);
		ret = machine__process_lost_event(machine, event, sample);
		break;
	default:
		ret = machine__process_event(machine, event, sample);
		break;
	}

	return ret;
}

static int trace__tool_process(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	return trace__process_event(trace, machine, event, sample);
}

static char *trace__machine__resolve_kernel_addr(void *vmachine, unsigned long long *addrp, char **modp)
{
	struct machine *machine = vmachine;

	if (machine->kptr_restrict_warned)
		return NULL;

	if (symbol_conf.kptr_restrict) {
		pr_warning("Kernel address maps (/proc/{kallsyms,modules}) are restricted.\n\n"
			   "Check /proc/sys/kernel/kptr_restrict.\n\n"
			   "Kernel samples will not be resolved.\n");
		machine->kptr_restrict_warned = true;
		return NULL;
	}

	return machine__resolve_kernel_addr(vmachine, addrp, modp);
}

static int trace__symbols_init(struct trace *trace, struct perf_evlist *evlist)
{
	int err = symbol__init(NULL);

	if (err)
		return err;

	trace->host = machine__new_host();
	if (trace->host == NULL)
		return -ENOMEM;

	if (trace_event__register_resolver(trace->host, trace__machine__resolve_kernel_addr) < 0)
		return -errno;

	err = __machine__synthesize_threads(trace->host, &trace->tool, &trace->opts.target,
					    evlist->threads, trace__tool_process, false,
					    trace->opts.proc_map_timeout);
	if (err)
		symbol__exit();

	return err;
}

static int syscall__set_arg_fmts(struct syscall *sc)
{
	struct format_field *field;
	int idx = 0, len;

	sc->arg_scnprintf = calloc(sc->nr_args, sizeof(void *));
	if (sc->arg_scnprintf == NULL)
		return -1;

	if (sc->fmt)
		sc->arg_parm = sc->fmt->arg_parm;

	for (field = sc->args; field; field = field->next) {
		if (sc->fmt && sc->fmt->arg_scnprintf[idx])
			sc->arg_scnprintf[idx] = sc->fmt->arg_scnprintf[idx];
		else if (strcmp(field->type, "const char *") == 0 &&
			 (strcmp(field->name, "filename") == 0 ||
			  strcmp(field->name, "path") == 0 ||
			  strcmp(field->name, "pathname") == 0))
			sc->arg_scnprintf[idx] = SCA_FILENAME;
		else if (field->flags & FIELD_IS_POINTER)
			sc->arg_scnprintf[idx] = syscall_arg__scnprintf_hex;
		else if (strcmp(field->type, "pid_t") == 0)
			sc->arg_scnprintf[idx] = SCA_PID;
		else if (strcmp(field->type, "umode_t") == 0)
			sc->arg_scnprintf[idx] = SCA_MODE_T;
		else if ((strcmp(field->type, "int") == 0 ||
			  strcmp(field->type, "unsigned int") == 0 ||
			  strcmp(field->type, "long") == 0) &&
			 (len = strlen(field->name)) >= 2 &&
			 strcmp(field->name + len - 2, "fd") == 0) {
			/*
			 * /sys/kernel/tracing/events/syscalls/sys_enter*
			 * egrep 'field:.*fd;' .../format|sed -r 's/.*field:([a-z ]+) [a-z_]*fd.+/\1/g'|sort|uniq -c
			 * 65 int
			 * 23 unsigned int
			 * 7 unsigned long
			 */
			sc->arg_scnprintf[idx] = SCA_FD;
		}
		++idx;
	}

	return 0;
}

static int trace__read_syscall_info(struct trace *trace, int id)
{
	char tp_name[128];
	struct syscall *sc;
	const char *name = syscalltbl__name(trace->sctbl, id);

	if (name == NULL)
		return -1;

	if (id > trace->syscalls.max) {
		struct syscall *nsyscalls = realloc(trace->syscalls.table, (id + 1) * sizeof(*sc));

		if (nsyscalls == NULL)
			return -1;

		if (trace->syscalls.max != -1) {
			memset(nsyscalls + trace->syscalls.max + 1, 0,
			       (id - trace->syscalls.max) * sizeof(*sc));
		} else {
			memset(nsyscalls, 0, (id + 1) * sizeof(*sc));
		}

		trace->syscalls.table = nsyscalls;
		trace->syscalls.max   = id;
	}

	sc = trace->syscalls.table + id;
	sc->name = name;

	sc->fmt  = syscall_fmt__find(sc->name);

	snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->name);
	sc->tp_format = trace_event__tp_format("syscalls", tp_name);

	if (IS_ERR(sc->tp_format) && sc->fmt && sc->fmt->alias) {
		snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->fmt->alias);
		sc->tp_format = trace_event__tp_format("syscalls", tp_name);
	}

	if (IS_ERR(sc->tp_format))
		return -1;

	sc->args = sc->tp_format->format.fields;
	sc->nr_args = sc->tp_format->format.nr_fields;
	/*
	 * We need to check and discard the first variable '__syscall_nr'
	 * or 'nr' that mean the syscall number. It is needless here.
	 * So drop '__syscall_nr' or 'nr' field but does not exist on older kernels.
	 */
	if (sc->args && (!strcmp(sc->args->name, "__syscall_nr") || !strcmp(sc->args->name, "nr"))) {
		sc->args = sc->args->next;
		--sc->nr_args;
	}

	sc->is_exit = !strcmp(name, "exit_group") || !strcmp(name, "exit");

	return syscall__set_arg_fmts(sc);
}

static int trace__validate_ev_qualifier(struct trace *trace)
{
	int err = 0, i;
	struct str_node *pos;

	trace->ev_qualifier_ids.nr = strlist__nr_entries(trace->ev_qualifier);
	trace->ev_qualifier_ids.entries = malloc(trace->ev_qualifier_ids.nr *
						 sizeof(trace->ev_qualifier_ids.entries[0]));

	if (trace->ev_qualifier_ids.entries == NULL) {
		fputs("Error:\tNot enough memory for allocating events qualifier ids\n",
		       trace->output);
		err = -EINVAL;
		goto out;
	}

	i = 0;

	strlist__for_each_entry(pos, trace->ev_qualifier) {
		const char *sc = pos->s;
		int id = syscalltbl__id(trace->sctbl, sc);

		if (id < 0) {
			if (err == 0) {
				fputs("Error:\tInvalid syscall ", trace->output);
				err = -EINVAL;
			} else {
				fputs(", ", trace->output);
			}

			fputs(sc, trace->output);
		}

		trace->ev_qualifier_ids.entries[i++] = id;
	}

	if (err < 0) {
		fputs("\nHint:\ttry 'perf list syscalls:sys_enter_*'"
		      "\nHint:\tand: 'man syscalls'\n", trace->output);
		zfree(&trace->ev_qualifier_ids.entries);
		trace->ev_qualifier_ids.nr = 0;
	}
out:
	return err;
}

/*
 * args is to be interpreted as a series of longs but we need to handle
 * 8-byte unaligned accesses. args points to raw_data within the event
 * and raw_data is guaranteed to be 8-byte unaligned because it is
 * preceded by raw_size which is a u32. So we need to copy args to a temp
 * variable to read it. Most notably this avoids extended load instructions
 * on unaligned addresses
 */

static size_t syscall__scnprintf_args(struct syscall *sc, char *bf, size_t size,
				      unsigned char *args, struct trace *trace,
				      struct thread *thread)
{
	size_t printed = 0;
	unsigned char *p;
	unsigned long val;

	if (sc->args != NULL) {
		struct format_field *field;
		u8 bit = 1;
		struct syscall_arg arg = {
			.idx	= 0,
			.mask	= 0,
			.trace  = trace,
			.thread = thread,
		};

		for (field = sc->args; field;
		     field = field->next, ++arg.idx, bit <<= 1) {
			if (arg.mask & bit)
				continue;

			/* special care for unaligned accesses */
			p = args + sizeof(unsigned long) * arg.idx;
			memcpy(&val, p, sizeof(val));

			/*
 			 * Suppress this argument if its value is zero and
 			 * and we don't have a string associated in an
 			 * strarray for it.
 			 */
			if (val == 0 &&
			    !(sc->arg_scnprintf &&
			      sc->arg_scnprintf[arg.idx] == SCA_STRARRAY &&
			      sc->arg_parm[arg.idx]))
				continue;

			printed += scnprintf(bf + printed, size - printed,
					     "%s%s: ", printed ? ", " : "", field->name);
			if (sc->arg_scnprintf && sc->arg_scnprintf[arg.idx]) {
				arg.val = val;
				if (sc->arg_parm)
					arg.parm = sc->arg_parm[arg.idx];
				printed += sc->arg_scnprintf[arg.idx](bf + printed,
								      size - printed, &arg);
			} else {
				printed += scnprintf(bf + printed, size - printed,
						     "%ld", val);
			}
		}
	} else if (IS_ERR(sc->tp_format)) {
		/*
		 * If we managed to read the tracepoint /format file, then we
		 * may end up not having any args, like with gettid(), so only
		 * print the raw args when we didn't manage to read it.
		 */
		int i = 0;

		while (i < 6) {
			/* special care for unaligned accesses */
			p = args + sizeof(unsigned long) * i;
			memcpy(&val, p, sizeof(val));
			printed += scnprintf(bf + printed, size - printed,
					     "%sarg%d: %ld",
					     printed ? ", " : "", i, val);
			++i;
		}
	}

	return printed;
}

typedef int (*tracepoint_handler)(struct trace *trace, struct perf_evsel *evsel,
				  union perf_event *event,
				  struct perf_sample *sample);

static struct syscall *trace__syscall_info(struct trace *trace,
					   struct perf_evsel *evsel, int id)
{

	if (id < 0) {

		/*
		 * XXX: Noticed on x86_64, reproduced as far back as 3.0.36, haven't tried
		 * before that, leaving at a higher verbosity level till that is
		 * explained. Reproduced with plain ftrace with:
		 *
		 * echo 1 > /t/events/raw_syscalls/sys_exit/enable
		 * grep "NR -1 " /t/trace_pipe
		 *
		 * After generating some load on the machine.
 		 */
		if (verbose > 1) {
			static u64 n;
			fprintf(trace->output, "Invalid syscall %d id, skipping (%s, %" PRIu64 ") ...\n",
				id, perf_evsel__name(evsel), ++n);
		}
		return NULL;
	}

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL) &&
	    trace__read_syscall_info(trace, id))
		goto out_cant_read;

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL))
		goto out_cant_read;

	return &trace->syscalls.table[id];

out_cant_read:
	if (verbose) {
		fprintf(trace->output, "Problems reading syscall %d", id);
		if (id <= trace->syscalls.max && trace->syscalls.table[id].name != NULL)
			fprintf(trace->output, "(%s)", trace->syscalls.table[id].name);
		fputs(" information\n", trace->output);
	}
	return NULL;
}

static void thread__update_stats(struct thread_trace *ttrace,
				 int id, struct perf_sample *sample)
{
	struct int_node *inode;
	struct stats *stats;
	u64 duration = 0;

	inode = intlist__findnew(ttrace->syscall_stats, id);
	if (inode == NULL)
		return;

	stats = inode->priv;
	if (stats == NULL) {
		stats = malloc(sizeof(struct stats));
		if (stats == NULL)
			return;
		init_stats(stats);
		inode->priv = stats;
	}

	if (ttrace->entry_time && sample->time > ttrace->entry_time)
		duration = sample->time - ttrace->entry_time;

	update_stats(stats, duration);
}

static int trace__printf_interrupted_entry(struct trace *trace, struct perf_sample *sample)
{
	struct thread_trace *ttrace;
	u64 duration;
	size_t printed;

	if (trace->current == NULL)
		return 0;

	ttrace = thread__priv(trace->current);

	if (!ttrace->entry_pending)
		return 0;

	duration = sample->time - ttrace->entry_time;

	printed  = trace__fprintf_entry_head(trace, trace->current, duration, sample->time, trace->output);
	printed += fprintf(trace->output, "%-70s) ...\n", ttrace->entry_str);
	ttrace->entry_pending = false;

	return printed;
}

static int trace__sys_enter(struct trace *trace, struct perf_evsel *evsel,
			    union perf_event *event __maybe_unused,
			    struct perf_sample *sample)
{
	char *msg;
	void *args;
	size_t printed = 0;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample), err = -1;
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	args = perf_evsel__sc_tp_ptr(evsel, args, sample);

	if (ttrace->entry_str == NULL) {
		ttrace->entry_str = malloc(trace__entry_str_size);
		if (!ttrace->entry_str)
			goto out_put;
	}

	if (!(trace->duration_filter || trace->summary_only || trace->min_stack))
		trace__printf_interrupted_entry(trace, sample);

	ttrace->entry_time = sample->time;
	msg = ttrace->entry_str;
	printed += scnprintf(msg + printed, trace__entry_str_size - printed, "%s(", sc->name);

	printed += syscall__scnprintf_args(sc, msg + printed, trace__entry_str_size - printed,
					   args, trace, thread);

	if (sc->is_exit) {
		if (!(trace->duration_filter || trace->summary_only || trace->min_stack)) {
			trace__fprintf_entry_head(trace, thread, 1, sample->time, trace->output);
			fprintf(trace->output, "%-70s)\n", ttrace->entry_str);
		}
	} else {
		ttrace->entry_pending = true;
		/* See trace__vfs_getname & trace__sys_exit */
		ttrace->filename.pending_open = false;
	}

	if (trace->current != thread) {
		thread__put(trace->current);
		trace->current = thread__get(thread);
	}
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int trace__resolve_callchain(struct trace *trace, struct perf_evsel *evsel,
				    struct perf_sample *sample,
				    struct callchain_cursor *cursor)
{
	struct addr_location al;

	if (machine__resolve(trace->host, &al, sample) < 0 ||
	    thread__resolve_callchain(al.thread, cursor, evsel, sample, NULL, NULL, trace->max_stack))
		return -1;

	return 0;
}

static int trace__fprintf_callchain(struct trace *trace, struct perf_sample *sample)
{
	/* TODO: user-configurable print_opts */
	const unsigned int print_opts = EVSEL__PRINT_SYM |
				        EVSEL__PRINT_DSO |
				        EVSEL__PRINT_UNKNOWN_AS_ADDR;

	return sample__fprintf_callchain(sample, 38, print_opts, &callchain_cursor, trace->output);
}

static int trace__sys_exit(struct trace *trace, struct perf_evsel *evsel,
			   union perf_event *event __maybe_unused,
			   struct perf_sample *sample)
{
	long ret;
	u64 duration = 0;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample), err = -1, callchain_ret = 0;
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	if (trace->summary)
		thread__update_stats(ttrace, id, sample);

	ret = perf_evsel__sc_tp_uint(evsel, ret, sample);

	if (id == trace->open_id && ret >= 0 && ttrace->filename.pending_open) {
		trace__set_fd_pathname(thread, ret, ttrace->filename.name);
		ttrace->filename.pending_open = false;
		++trace->stats.vfs_getname;
	}

	ttrace->exit_time = sample->time;

	if (ttrace->entry_time) {
		duration = sample->time - ttrace->entry_time;
		if (trace__filter_duration(trace, duration))
			goto out;
	} else if (trace->duration_filter)
		goto out;

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out;
			callchain_ret = 1;
		}
	}

	if (trace->summary_only)
		goto out;

	trace__fprintf_entry_head(trace, thread, duration, sample->time, trace->output);

	if (ttrace->entry_pending) {
		fprintf(trace->output, "%-70s", ttrace->entry_str);
	} else {
		fprintf(trace->output, " ... [");
		color_fprintf(trace->output, PERF_COLOR_YELLOW, "continued");
		fprintf(trace->output, "]: %s()", sc->name);
	}

	if (sc->fmt == NULL) {
signed_print:
		fprintf(trace->output, ") = %ld", ret);
	} else if (ret < 0 && (sc->fmt->errmsg || sc->fmt->errpid)) {
		char bf[STRERR_BUFSIZE];
		const char *emsg = str_error_r(-ret, bf, sizeof(bf)),
			   *e = audit_errno_to_name(-ret);

		fprintf(trace->output, ") = -1 %s %s", e, emsg);
	} else if (ret == 0 && sc->fmt->timeout)
		fprintf(trace->output, ") = 0 Timeout");
	else if (sc->fmt->hexret)
		fprintf(trace->output, ") = %#lx", ret);
	else if (sc->fmt->errpid) {
		struct thread *child = machine__find_thread(trace->host, ret, ret);

		if (child != NULL) {
			fprintf(trace->output, ") = %ld", ret);
			if (child->comm_set)
				fprintf(trace->output, " (%s)", thread__comm_str(child));
			thread__put(child);
		}
	} else
		goto signed_print;

	fputc('\n', trace->output);

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", perf_evsel__name(evsel));
out:
	ttrace->entry_pending = false;
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int trace__vfs_getname(struct trace *trace, struct perf_evsel *evsel,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample)
{
	struct thread *thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	struct thread_trace *ttrace;
	size_t filename_len, entry_str_len, to_move;
	ssize_t remaining_space;
	char *pos;
	const char *filename = perf_evsel__rawptr(evsel, sample, "pathname");

	if (!thread)
		goto out;

	ttrace = thread__priv(thread);
	if (!ttrace)
		goto out;

	filename_len = strlen(filename);

	if (ttrace->filename.namelen < filename_len) {
		char *f = realloc(ttrace->filename.name, filename_len + 1);

		if (f == NULL)
				goto out;

		ttrace->filename.namelen = filename_len;
		ttrace->filename.name = f;
	}

	strcpy(ttrace->filename.name, filename);
	ttrace->filename.pending_open = true;

	if (!ttrace->filename.ptr)
		goto out;

	entry_str_len = strlen(ttrace->entry_str);
	remaining_space = trace__entry_str_size - entry_str_len - 1; /* \0 */
	if (remaining_space <= 0)
		goto out;

	if (filename_len > (size_t)remaining_space) {
		filename += filename_len - remaining_space;
		filename_len = remaining_space;
	}

	to_move = entry_str_len - ttrace->filename.entry_str_pos + 1; /* \0 */
	pos = ttrace->entry_str + ttrace->filename.entry_str_pos;
	memmove(pos + filename_len, pos, to_move);
	memcpy(pos, filename, filename_len);

	ttrace->filename.ptr = 0;
	ttrace->filename.entry_str_pos = 0;
out:
	return 0;
}

static int trace__sched_stat_runtime(struct trace *trace, struct perf_evsel *evsel,
				     union perf_event *event __maybe_unused,
				     struct perf_sample *sample)
{
        u64 runtime = perf_evsel__intval(evsel, sample, "runtime");
	double runtime_ms = (double)runtime / NSEC_PER_MSEC;
	struct thread *thread = machine__findnew_thread(trace->host,
							sample->pid,
							sample->tid);
	struct thread_trace *ttrace = thread__trace(thread, trace->output);

	if (ttrace == NULL)
		goto out_dump;

	ttrace->runtime_ms += runtime_ms;
	trace->runtime_ms += runtime_ms;
	thread__put(thread);
	return 0;

out_dump:
	fprintf(trace->output, "%s: comm=%s,pid=%u,runtime=%" PRIu64 ",vruntime=%" PRIu64 ")\n",
	       evsel->name,
	       perf_evsel__strval(evsel, sample, "comm"),
	       (pid_t)perf_evsel__intval(evsel, sample, "pid"),
	       runtime,
	       perf_evsel__intval(evsel, sample, "vruntime"));
	thread__put(thread);
	return 0;
}

static void bpf_output__printer(enum binary_printer_ops op,
				unsigned int val, void *extra)
{
	FILE *output = extra;
	unsigned char ch = (unsigned char)val;

	switch (op) {
	case BINARY_PRINT_CHAR_DATA:
		fprintf(output, "%c", isprint(ch) ? ch : '.');
		break;
	case BINARY_PRINT_DATA_BEGIN:
	case BINARY_PRINT_LINE_BEGIN:
	case BINARY_PRINT_ADDR:
	case BINARY_PRINT_NUM_DATA:
	case BINARY_PRINT_NUM_PAD:
	case BINARY_PRINT_SEP:
	case BINARY_PRINT_CHAR_PAD:
	case BINARY_PRINT_LINE_END:
	case BINARY_PRINT_DATA_END:
	default:
		break;
	}
}

static void bpf_output__fprintf(struct trace *trace,
				struct perf_sample *sample)
{
	print_binary(sample->raw_data, sample->raw_size, 8,
		     bpf_output__printer, trace->output);
}

static int trace__event_handler(struct trace *trace, struct perf_evsel *evsel,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample)
{
	int callchain_ret = 0;

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out;
			callchain_ret = 1;
		}
	}

	trace__printf_interrupted_entry(trace, sample);
	trace__fprintf_tstamp(trace, sample->time, trace->output);

	if (trace->trace_syscalls)
		fprintf(trace->output, "(         ): ");

	fprintf(trace->output, "%s:", evsel->name);

	if (perf_evsel__is_bpf_output(evsel)) {
		bpf_output__fprintf(trace, sample);
	} else if (evsel->tp_format) {
		event_format__fprintf(evsel->tp_format, sample->cpu,
				      sample->raw_data, sample->raw_size,
				      trace->output);
	}

	fprintf(trace->output, ")\n");

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", perf_evsel__name(evsel));
out:
	return 0;
}

static void print_location(FILE *f, struct perf_sample *sample,
			   struct addr_location *al,
			   bool print_dso, bool print_sym)
{

	if ((verbose || print_dso) && al->map)
		fprintf(f, "%s@", al->map->dso->long_name);

	if ((verbose || print_sym) && al->sym)
		fprintf(f, "%s+0x%" PRIx64, al->sym->name,
			al->addr - al->sym->start);
	else if (al->map)
		fprintf(f, "0x%" PRIx64, al->addr);
	else
		fprintf(f, "0x%" PRIx64, sample->addr);
}

static int trace__pgfault(struct trace *trace,
			  struct perf_evsel *evsel,
			  union perf_event *event __maybe_unused,
			  struct perf_sample *sample)
{
	struct thread *thread;
	struct addr_location al;
	char map_type = 'd';
	struct thread_trace *ttrace;
	int err = -1;
	int callchain_ret = 0;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out_put;
			callchain_ret = 1;
		}
	}

	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	if (evsel->attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ)
		ttrace->pfmaj++;
	else
		ttrace->pfmin++;

	if (trace->summary_only)
		goto out;

	thread__find_addr_location(thread, sample->cpumode, MAP__FUNCTION,
			      sample->ip, &al);

	trace__fprintf_entry_head(trace, thread, 0, sample->time, trace->output);

	fprintf(trace->output, "%sfault [",
		evsel->attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ ?
		"maj" : "min");

	print_location(trace->output, sample, &al, false, true);

	fprintf(trace->output, "] => ");

	thread__find_addr_location(thread, sample->cpumode, MAP__VARIABLE,
				   sample->addr, &al);

	if (!al.map) {
		thread__find_addr_location(thread, sample->cpumode,
					   MAP__FUNCTION, sample->addr, &al);

		if (al.map)
			map_type = 'x';
		else
			map_type = '?';
	}

	print_location(trace->output, sample, &al, true, false);

	fprintf(trace->output, " (%c%c)\n", map_type, al.level);

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", perf_evsel__name(evsel));
out:
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static bool skip_sample(struct trace *trace, struct perf_sample *sample)
{
	if ((trace->pid_list && intlist__find(trace->pid_list, sample->pid)) ||
	    (trace->tid_list && intlist__find(trace->tid_list, sample->tid)))
		return false;

	if (trace->pid_list || trace->tid_list)
		return true;

	return false;
}

static void trace__set_base_time(struct trace *trace,
				 struct perf_evsel *evsel,
				 struct perf_sample *sample)
{
	/*
	 * BPF events were not setting PERF_SAMPLE_TIME, so be more robust
	 * and don't use sample->time unconditionally, we may end up having
	 * some other event in the future without PERF_SAMPLE_TIME for good
	 * reason, i.e. we may not be interested in its timestamps, just in
	 * it taking place, picking some piece of information when it
	 * appears in our event stream (vfs_getname comes to mind).
	 */
	if (trace->base_time == 0 && !trace->full_time &&
	    (evsel->attr.sample_type & PERF_SAMPLE_TIME))
		trace->base_time = sample->time;
}

static int trace__process_sample(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_sample *sample,
				 struct perf_evsel *evsel,
				 struct machine *machine __maybe_unused)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	int err = 0;

	tracepoint_handler handler = evsel->handler;

	if (skip_sample(trace, sample))
		return 0;

	trace__set_base_time(trace, evsel, sample);

	if (handler) {
		++trace->nr_events;
		handler(trace, evsel, event, sample);
	}

	return err;
}

static int parse_target_str(struct trace *trace)
{
	if (trace->opts.target.pid) {
		trace->pid_list = intlist__new(trace->opts.target.pid);
		if (trace->pid_list == NULL) {
			pr_err("Error parsing process id string\n");
			return -EINVAL;
		}
	}

	if (trace->opts.target.tid) {
		trace->tid_list = intlist__new(trace->opts.target.tid);
		if (trace->tid_list == NULL) {
			pr_err("Error parsing thread id string\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int trace__record(struct trace *trace, int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char * const record_args[] = {
		"record",
		"-R",
		"-m", "1024",
		"-c", "1",
	};

	const char * const sc_args[] = { "-e", };
	unsigned int sc_args_nr = ARRAY_SIZE(sc_args);
	const char * const majpf_args[] = { "-e", "major-faults" };
	unsigned int majpf_args_nr = ARRAY_SIZE(majpf_args);
	const char * const minpf_args[] = { "-e", "minor-faults" };
	unsigned int minpf_args_nr = ARRAY_SIZE(minpf_args);

	/* +1 is for the event string below */
	rec_argc = ARRAY_SIZE(record_args) + sc_args_nr + 1 +
		majpf_args_nr + minpf_args_nr + argc;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	j = 0;
	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[j++] = record_args[i];

	if (trace->trace_syscalls) {
		for (i = 0; i < sc_args_nr; i++)
			rec_argv[j++] = sc_args[i];

		/* event string may be different for older kernels - e.g., RHEL6 */
		if (is_valid_tracepoint("raw_syscalls:sys_enter"))
			rec_argv[j++] = "raw_syscalls:sys_enter,raw_syscalls:sys_exit";
		else if (is_valid_tracepoint("syscalls:sys_enter"))
			rec_argv[j++] = "syscalls:sys_enter,syscalls:sys_exit";
		else {
			pr_err("Neither raw_syscalls nor syscalls events exist.\n");
			return -1;
		}
	}

	if (trace->trace_pgfaults & TRACE_PFMAJ)
		for (i = 0; i < majpf_args_nr; i++)
			rec_argv[j++] = majpf_args[i];

	if (trace->trace_pgfaults & TRACE_PFMIN)
		for (i = 0; i < minpf_args_nr; i++)
			rec_argv[j++] = minpf_args[i];

	for (i = 0; i < (unsigned int)argc; i++)
		rec_argv[j++] = argv[i];

	return cmd_record(j, rec_argv, NULL);
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp);

static bool perf_evlist__add_vfs_getname(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evsel__newtp("probe", "vfs_getname");

	if (IS_ERR(evsel))
		return false;

	if (perf_evsel__field(evsel, "pathname") == NULL) {
		perf_evsel__delete(evsel);
		return false;
	}

	evsel->handler = trace__vfs_getname;
	perf_evlist__add(evlist, evsel);
	return true;
}

static struct perf_evsel *perf_evsel__new_pgfault(u64 config)
{
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.mmap_data = 1,
	};

	attr.config = config;
	attr.sample_period = 1;

	event_attr_init(&attr);

	evsel = perf_evsel__new(&attr);
	if (evsel)
		evsel->handler = trace__pgfault;

	return evsel;
}

static void trace__handle_event(struct trace *trace, union perf_event *event, struct perf_sample *sample)
{
	const u32 type = event->header.type;
	struct perf_evsel *evsel;

	if (type != PERF_RECORD_SAMPLE) {
		trace__process_event(trace, trace->host, event, sample);
		return;
	}

	evsel = perf_evlist__id2evsel(trace->evlist, sample->id);
	if (evsel == NULL) {
		fprintf(trace->output, "Unknown tp ID %" PRIu64 ", skipping...\n", sample->id);
		return;
	}

	trace__set_base_time(trace, evsel, sample);

	if (evsel->attr.type == PERF_TYPE_TRACEPOINT &&
	    sample->raw_data == NULL) {
		fprintf(trace->output, "%s sample with no payload for tid: %d, cpu %d, raw_size=%d, skipping...\n",
		       perf_evsel__name(evsel), sample->tid,
		       sample->cpu, sample->raw_size);
	} else {
		tracepoint_handler handler = evsel->handler;
		handler(trace, evsel, event, sample);
	}
}

static int trace__add_syscall_newtp(struct trace *trace)
{
	int ret = -1;
	struct perf_evlist *evlist = trace->evlist;
	struct perf_evsel *sys_enter, *sys_exit;

	sys_enter = perf_evsel__syscall_newtp("sys_enter", trace__sys_enter);
	if (sys_enter == NULL)
		goto out;

	if (perf_evsel__init_sc_tp_ptr_field(sys_enter, args))
		goto out_delete_sys_enter;

	sys_exit = perf_evsel__syscall_newtp("sys_exit", trace__sys_exit);
	if (sys_exit == NULL)
		goto out_delete_sys_enter;

	if (perf_evsel__init_sc_tp_uint_field(sys_exit, ret))
		goto out_delete_sys_exit;

	perf_evlist__add(evlist, sys_enter);
	perf_evlist__add(evlist, sys_exit);

	if (callchain_param.enabled && !trace->kernel_syscallchains) {
		/*
		 * We're interested only in the user space callchain
		 * leading to the syscall, allow overriding that for
		 * debugging reasons using --kernel_syscall_callchains
		 */
		sys_exit->attr.exclude_callchain_kernel = 1;
	}

	trace->syscalls.events.sys_enter = sys_enter;
	trace->syscalls.events.sys_exit  = sys_exit;

	ret = 0;
out:
	return ret;

out_delete_sys_exit:
	perf_evsel__delete_priv(sys_exit);
out_delete_sys_enter:
	perf_evsel__delete_priv(sys_enter);
	goto out;
}

static int trace__set_ev_qualifier_filter(struct trace *trace)
{
	int err = -1;
	struct perf_evsel *sys_exit;
	char *filter = asprintf_expr_inout_ints("id", !trace->not_ev_qualifier,
						trace->ev_qualifier_ids.nr,
						trace->ev_qualifier_ids.entries);

	if (filter == NULL)
		goto out_enomem;

	if (!perf_evsel__append_tp_filter(trace->syscalls.events.sys_enter,
					  filter)) {
		sys_exit = trace->syscalls.events.sys_exit;
		err = perf_evsel__append_tp_filter(sys_exit, filter);
	}

	free(filter);
out:
	return err;
out_enomem:
	errno = ENOMEM;
	goto out;
}

static int trace__run(struct trace *trace, int argc, const char **argv)
{
	struct perf_evlist *evlist = trace->evlist;
	struct perf_evsel *evsel, *pgfault_maj = NULL, *pgfault_min = NULL;
	int err = -1, i;
	unsigned long before;
	const bool forks = argc > 0;
	bool draining = false;

	trace->live = true;

	if (trace->trace_syscalls && trace__add_syscall_newtp(trace))
		goto out_error_raw_syscalls;

	if (trace->trace_syscalls)
		trace->vfs_getname = perf_evlist__add_vfs_getname(evlist);

	if ((trace->trace_pgfaults & TRACE_PFMAJ)) {
		pgfault_maj = perf_evsel__new_pgfault(PERF_COUNT_SW_PAGE_FAULTS_MAJ);
		if (pgfault_maj == NULL)
			goto out_error_mem;
		perf_evlist__add(evlist, pgfault_maj);
	}

	if ((trace->trace_pgfaults & TRACE_PFMIN)) {
		pgfault_min = perf_evsel__new_pgfault(PERF_COUNT_SW_PAGE_FAULTS_MIN);
		if (pgfault_min == NULL)
			goto out_error_mem;
		perf_evlist__add(evlist, pgfault_min);
	}

	if (trace->sched &&
	    perf_evlist__add_newtp(evlist, "sched", "sched_stat_runtime",
				   trace__sched_stat_runtime))
		goto out_error_sched_stat_runtime;

	err = perf_evlist__create_maps(evlist, &trace->opts.target);
	if (err < 0) {
		fprintf(trace->output, "Problems parsing the target to trace, check your options!\n");
		goto out_delete_evlist;
	}

	err = trace__symbols_init(trace, evlist);
	if (err < 0) {
		fprintf(trace->output, "Problems initializing symbol libraries!\n");
		goto out_delete_evlist;
	}

	perf_evlist__config(evlist, &trace->opts, NULL);

	if (callchain_param.enabled) {
		bool use_identifier = false;

		if (trace->syscalls.events.sys_exit) {
			perf_evsel__config_callchain(trace->syscalls.events.sys_exit,
						     &trace->opts, &callchain_param);
			use_identifier = true;
		}

		if (pgfault_maj) {
			perf_evsel__config_callchain(pgfault_maj, &trace->opts, &callchain_param);
			use_identifier = true;
		}

		if (pgfault_min) {
			perf_evsel__config_callchain(pgfault_min, &trace->opts, &callchain_param);
			use_identifier = true;
		}

		if (use_identifier) {
		       /*
			* Now we have evsels with different sample_ids, use
			* PERF_SAMPLE_IDENTIFIER to map from sample to evsel
			* from a fixed position in each ring buffer record.
			*
			* As of this the changeset introducing this comment, this
			* isn't strictly needed, as the fields that can come before
			* PERF_SAMPLE_ID are all used, but we'll probably disable
			* some of those for things like copying the payload of
			* pointer syscall arguments, and for vfs_getname we don't
			* need PERF_SAMPLE_ADDR and PERF_SAMPLE_IP, so do this
			* here as a warning we need to use PERF_SAMPLE_IDENTIFIER.
			*/
			perf_evlist__set_sample_bit(evlist, IDENTIFIER);
			perf_evlist__reset_sample_bit(evlist, ID);
		}
	}

	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (forks) {
		err = perf_evlist__prepare_workload(evlist, &trace->opts.target,
						    argv, false, NULL);
		if (err < 0) {
			fprintf(trace->output, "Couldn't run the workload!\n");
			goto out_delete_evlist;
		}
	}

	err = perf_evlist__open(evlist);
	if (err < 0)
		goto out_error_open;

	err = bpf__apply_obj_config();
	if (err) {
		char errbuf[BUFSIZ];

		bpf__strerror_apply_obj_config(err, errbuf, sizeof(errbuf));
		pr_err("ERROR: Apply config to BPF failed: %s\n",
			 errbuf);
		goto out_error_open;
	}

	/*
	 * Better not use !target__has_task() here because we need to cover the
	 * case where no threads were specified in the command line, but a
	 * workload was, and in that case we will fill in the thread_map when
	 * we fork the workload in perf_evlist__prepare_workload.
	 */
	if (trace->filter_pids.nr > 0)
		err = perf_evlist__set_filter_pids(evlist, trace->filter_pids.nr, trace->filter_pids.entries);
	else if (thread_map__pid(evlist->threads, 0) == -1)
		err = perf_evlist__set_filter_pid(evlist, getpid());

	if (err < 0)
		goto out_error_mem;

	if (trace->ev_qualifier_ids.nr > 0) {
		err = trace__set_ev_qualifier_filter(trace);
		if (err < 0)
			goto out_errno;

		pr_debug("event qualifier tracepoint filter: %s\n",
			 trace->syscalls.events.sys_exit->filter);
	}

	err = perf_evlist__apply_filters(evlist, &evsel);
	if (err < 0)
		goto out_error_apply_filters;

	err = perf_evlist__mmap(evlist, trace->opts.mmap_pages, false);
	if (err < 0)
		goto out_error_mmap;

	if (!target__none(&trace->opts.target))
		perf_evlist__enable(evlist);

	if (forks)
		perf_evlist__start_workload(evlist);

	trace->multiple_threads = thread_map__pid(evlist->threads, 0) == -1 ||
				  evlist->threads->nr > 1 ||
				  perf_evlist__first(evlist)->attr.inherit;
again:
	before = trace->nr_events;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		union perf_event *event;

		while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
			struct perf_sample sample;

			++trace->nr_events;

			err = perf_evlist__parse_sample(evlist, event, &sample);
			if (err) {
				fprintf(trace->output, "Can't parse sample, err = %d, skipping...\n", err);
				goto next_event;
			}

			trace__handle_event(trace, event, &sample);
next_event:
			perf_evlist__mmap_consume(evlist, i);

			if (interrupted)
				goto out_disable;

			if (done && !draining) {
				perf_evlist__disable(evlist);
				draining = true;
			}
		}
	}

	if (trace->nr_events == before) {
		int timeout = done ? 100 : -1;

		if (!draining && perf_evlist__poll(evlist, timeout) > 0) {
			if (perf_evlist__filter_pollfd(evlist, POLLERR | POLLHUP) == 0)
				draining = true;

			goto again;
		}
	} else {
		goto again;
	}

out_disable:
	thread__zput(trace->current);

	perf_evlist__disable(evlist);

	if (!err) {
		if (trace->summary)
			trace__fprintf_thread_summary(trace, trace->output);

		if (trace->show_tool_stats) {
			fprintf(trace->output, "Stats:\n "
					       " vfs_getname : %" PRIu64 "\n"
					       " proc_getname: %" PRIu64 "\n",
				trace->stats.vfs_getname,
				trace->stats.proc_getname);
		}
	}

out_delete_evlist:
	perf_evlist__delete(evlist);
	trace->evlist = NULL;
	trace->live = false;
	return err;
{
	char errbuf[BUFSIZ];

out_error_sched_stat_runtime:
	tracing_path__strerror_open_tp(errno, errbuf, sizeof(errbuf), "sched", "sched_stat_runtime");
	goto out_error;

out_error_raw_syscalls:
	tracing_path__strerror_open_tp(errno, errbuf, sizeof(errbuf), "raw_syscalls", "sys_(enter|exit)");
	goto out_error;

out_error_mmap:
	perf_evlist__strerror_mmap(evlist, errno, errbuf, sizeof(errbuf));
	goto out_error;

out_error_open:
	perf_evlist__strerror_open(evlist, errno, errbuf, sizeof(errbuf));

out_error:
	fprintf(trace->output, "%s\n", errbuf);
	goto out_delete_evlist;

out_error_apply_filters:
	fprintf(trace->output,
		"Failed to set filter \"%s\" on event %s with %d (%s)\n",
		evsel->filter, perf_evsel__name(evsel), errno,
		str_error_r(errno, errbuf, sizeof(errbuf)));
	goto out_delete_evlist;
}
out_error_mem:
	fprintf(trace->output, "Not enough memory to run!\n");
	goto out_delete_evlist;

out_errno:
	fprintf(trace->output, "errno=%d,%s\n", errno, strerror(errno));
	goto out_delete_evlist;
}

static int trace__replay(struct trace *trace)
{
	const struct perf_evsel_str_handler handlers[] = {
		{ "probe:vfs_getname",	     trace__vfs_getname, },
	};
	struct perf_data_file file = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = trace->force,
	};
	struct perf_session *session;
	struct perf_evsel *evsel;
	int err = -1;

	trace->tool.sample	  = trace__process_sample;
	trace->tool.mmap	  = perf_event__process_mmap;
	trace->tool.mmap2	  = perf_event__process_mmap2;
	trace->tool.comm	  = perf_event__process_comm;
	trace->tool.exit	  = perf_event__process_exit;
	trace->tool.fork	  = perf_event__process_fork;
	trace->tool.attr	  = perf_event__process_attr;
	trace->tool.tracing_data = perf_event__process_tracing_data;
	trace->tool.build_id	  = perf_event__process_build_id;

	trace->tool.ordered_events = true;
	trace->tool.ordering_requires_timestamps = true;

	/* add tid to output */
	trace->multiple_threads = true;

	session = perf_session__new(&file, false, &trace->tool);
	if (session == NULL)
		return -1;

	if (symbol__init(&session->header.env) < 0)
		goto out;

	trace->host = &session->machines.host;

	err = perf_session__set_tracepoints_handlers(session, handlers);
	if (err)
		goto out;

	evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
						     "raw_syscalls:sys_enter");
	/* older kernels have syscalls tp versus raw_syscalls */
	if (evsel == NULL)
		evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
							     "syscalls:sys_enter");

	if (evsel &&
	    (perf_evsel__init_syscall_tp(evsel, trace__sys_enter) < 0 ||
	    perf_evsel__init_sc_tp_ptr_field(evsel, args))) {
		pr_err("Error during initialize raw_syscalls:sys_enter event\n");
		goto out;
	}

	evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
						     "raw_syscalls:sys_exit");
	if (evsel == NULL)
		evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
							     "syscalls:sys_exit");
	if (evsel &&
	    (perf_evsel__init_syscall_tp(evsel, trace__sys_exit) < 0 ||
	    perf_evsel__init_sc_tp_uint_field(evsel, ret))) {
		pr_err("Error during initialize raw_syscalls:sys_exit event\n");
		goto out;
	}

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->attr.type == PERF_TYPE_SOFTWARE &&
		    (evsel->attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ ||
		     evsel->attr.config == PERF_COUNT_SW_PAGE_FAULTS_MIN ||
		     evsel->attr.config == PERF_COUNT_SW_PAGE_FAULTS))
			evsel->handler = trace__pgfault;
	}

	err = parse_target_str(trace);
	if (err != 0)
		goto out;

	setup_pager();

	err = perf_session__process_events(session);
	if (err)
		pr_err("Failed to process events, error %d", err);

	else if (trace->summary)
		trace__fprintf_thread_summary(trace, trace->output);

out:
	perf_session__delete(session);

	return err;
}

static size_t trace__fprintf_threads_header(FILE *fp)
{
	size_t printed;

	printed  = fprintf(fp, "\n Summary of events:\n\n");

	return printed;
}

DEFINE_RESORT_RB(syscall_stats, a->msecs > b->msecs,
	struct stats 	*stats;
	double		msecs;
	int		syscall;
)
{
	struct int_node *source = rb_entry(nd, struct int_node, rb_node);
	struct stats *stats = source->priv;

	entry->syscall = source->i;
	entry->stats   = stats;
	entry->msecs   = stats ? (u64)stats->n * (avg_stats(stats) / NSEC_PER_MSEC) : 0;
}

static size_t thread__dump_stats(struct thread_trace *ttrace,
				 struct trace *trace, FILE *fp)
{
	size_t printed = 0;
	struct syscall *sc;
	struct rb_node *nd;
	DECLARE_RESORT_RB_INTLIST(syscall_stats, ttrace->syscall_stats);

	if (syscall_stats == NULL)
		return 0;

	printed += fprintf(fp, "\n");

	printed += fprintf(fp, "   syscall            calls    total       min       avg       max      stddev\n");
	printed += fprintf(fp, "                               (msec)    (msec)    (msec)    (msec)        (%%)\n");
	printed += fprintf(fp, "   --------------- -------- --------- --------- --------- ---------     ------\n");

	resort_rb__for_each_entry(nd, syscall_stats) {
		struct stats *stats = syscall_stats_entry->stats;
		if (stats) {
			double min = (double)(stats->min) / NSEC_PER_MSEC;
			double max = (double)(stats->max) / NSEC_PER_MSEC;
			double avg = avg_stats(stats);
			double pct;
			u64 n = (u64) stats->n;

			pct = avg ? 100.0 * stddev_stats(stats)/avg : 0.0;
			avg /= NSEC_PER_MSEC;

			sc = &trace->syscalls.table[syscall_stats_entry->syscall];
			printed += fprintf(fp, "   %-15s", sc->name);
			printed += fprintf(fp, " %8" PRIu64 " %9.3f %9.3f %9.3f",
					   n, syscall_stats_entry->msecs, min, avg);
			printed += fprintf(fp, " %9.3f %9.2f%%\n", max, pct);
		}
	}

	resort_rb__delete(syscall_stats);
	printed += fprintf(fp, "\n\n");

	return printed;
}

static size_t trace__fprintf_thread(FILE *fp, struct thread *thread, struct trace *trace)
{
	size_t printed = 0;
	struct thread_trace *ttrace = thread__priv(thread);
	double ratio;

	if (ttrace == NULL)
		return 0;

	ratio = (double)ttrace->nr_events / trace->nr_events * 100.0;

	printed += fprintf(fp, " %s (%d), ", thread__comm_str(thread), thread->tid);
	printed += fprintf(fp, "%lu events, ", ttrace->nr_events);
	printed += fprintf(fp, "%.1f%%", ratio);
	if (ttrace->pfmaj)
		printed += fprintf(fp, ", %lu majfaults", ttrace->pfmaj);
	if (ttrace->pfmin)
		printed += fprintf(fp, ", %lu minfaults", ttrace->pfmin);
	if (trace->sched)
		printed += fprintf(fp, ", %.3f msec\n", ttrace->runtime_ms);
	else if (fputc('\n', fp) != EOF)
		++printed;

	printed += thread__dump_stats(ttrace, trace, fp);

	return printed;
}

static unsigned long thread__nr_events(struct thread_trace *ttrace)
{
	return ttrace ? ttrace->nr_events : 0;
}

DEFINE_RESORT_RB(threads, (thread__nr_events(a->thread->priv) < thread__nr_events(b->thread->priv)),
	struct thread *thread;
)
{
	entry->thread = rb_entry(nd, struct thread, rb_node);
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp)
{
	DECLARE_RESORT_RB_MACHINE_THREADS(threads, trace->host);
	size_t printed = trace__fprintf_threads_header(fp);
	struct rb_node *nd;

	if (threads == NULL) {
		fprintf(fp, "%s", "Error sorting output by nr_events!\n");
		return 0;
	}

	resort_rb__for_each_entry(nd, threads)
		printed += trace__fprintf_thread(fp, threads_entry->thread, trace);

	resort_rb__delete(threads);

	return printed;
}

static int trace__set_duration(const struct option *opt, const char *str,
			       int unset __maybe_unused)
{
	struct trace *trace = opt->value;

	trace->duration_filter = atof(str);
	return 0;
}

static int trace__set_filter_pids(const struct option *opt, const char *str,
				  int unset __maybe_unused)
{
	int ret = -1;
	size_t i;
	struct trace *trace = opt->value;
	/*
	 * FIXME: introduce a intarray class, plain parse csv and create a
	 * { int nr, int entries[] } struct...
	 */
	struct intlist *list = intlist__new(str);

	if (list == NULL)
		return -1;

	i = trace->filter_pids.nr = intlist__nr_entries(list) + 1;
	trace->filter_pids.entries = calloc(i, sizeof(pid_t));

	if (trace->filter_pids.entries == NULL)
		goto out;

	trace->filter_pids.entries[0] = getpid();

	for (i = 1; i < trace->filter_pids.nr; ++i)
		trace->filter_pids.entries[i] = intlist__entry(list, i - 1)->i;

	intlist__delete(list);
	ret = 0;
out:
	return ret;
}

static int trace__open_output(struct trace *trace, const char *filename)
{
	struct stat st;

	if (!stat(filename, &st) && st.st_size) {
		char oldname[PATH_MAX];

		scnprintf(oldname, sizeof(oldname), "%s.old", filename);
		unlink(oldname);
		rename(filename, oldname);
	}

	trace->output = fopen(filename, "w");

	return trace->output == NULL ? -errno : 0;
}

static int parse_pagefaults(const struct option *opt, const char *str,
			    int unset __maybe_unused)
{
	int *trace_pgfaults = opt->value;

	if (strcmp(str, "all") == 0)
		*trace_pgfaults |= TRACE_PFMAJ | TRACE_PFMIN;
	else if (strcmp(str, "maj") == 0)
		*trace_pgfaults |= TRACE_PFMAJ;
	else if (strcmp(str, "min") == 0)
		*trace_pgfaults |= TRACE_PFMIN;
	else
		return -1;

	return 0;
}

static void evlist__set_evsel_handler(struct perf_evlist *evlist, void *handler)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		evsel->handler = handler;
}

int cmd_trace(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char *trace_usage[] = {
		"perf trace [<options>] [<command>]",
		"perf trace [<options>] -- <command> [<options>]",
		"perf trace record [<options>] [<command>]",
		"perf trace record [<options>] -- <command> [<options>]",
		NULL
	};
	struct trace trace = {
		.syscalls = {
			. max = -1,
		},
		.opts = {
			.target = {
				.uid	   = UINT_MAX,
				.uses_mmap = true,
			},
			.user_freq     = UINT_MAX,
			.user_interval = ULLONG_MAX,
			.no_buffering  = true,
			.mmap_pages    = UINT_MAX,
			.proc_map_timeout  = 500,
		},
		.output = stderr,
		.show_comm = true,
		.trace_syscalls = true,
		.kernel_syscallchains = false,
		.max_stack = UINT_MAX,
	};
	const char *output_name = NULL;
	const char *ev_qualifier_str = NULL;
	const struct option trace_options[] = {
	OPT_CALLBACK(0, "event", &trace.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_BOOLEAN(0, "comm", &trace.show_comm,
		    "show the thread COMM next to its id"),
	OPT_BOOLEAN(0, "tool_stats", &trace.show_tool_stats, "show tool stats"),
	OPT_STRING('e', "expr", &ev_qualifier_str, "expr", "list of syscalls to trace"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_STRING('i', "input", &input_name, "file", "Analyze events in file"),
	OPT_STRING('p', "pid", &trace.opts.target.pid, "pid",
		    "trace events on existing process id"),
	OPT_STRING('t', "tid", &trace.opts.target.tid, "tid",
		    "trace events on existing thread id"),
	OPT_CALLBACK(0, "filter-pids", &trace, "CSV list of pids",
		     "pids to filter (by the kernel)", trace__set_filter_pids),
	OPT_BOOLEAN('a', "all-cpus", &trace.opts.target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &trace.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_BOOLEAN(0, "no-inherit", &trace.opts.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_CALLBACK('m', "mmap-pages", &trace.opts.mmap_pages, "pages",
		     "number of mmap data pages",
		     perf_evlist__parse_mmap_pages),
	OPT_STRING('u', "uid", &trace.opts.target.uid_str, "user",
		   "user to profile"),
	OPT_CALLBACK(0, "duration", &trace, "float",
		     "show only events with duration > N.M ms",
		     trace__set_duration),
	OPT_BOOLEAN(0, "sched", &trace.sched, "show blocking scheduler events"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_BOOLEAN('T', "time", &trace.full_time,
		    "Show full timestamp, not time relative to first start"),
	OPT_BOOLEAN('s', "summary", &trace.summary_only,
		    "Show only syscall summary with statistics"),
	OPT_BOOLEAN('S', "with-summary", &trace.summary,
		    "Show all syscalls and summary with statistics"),
	OPT_CALLBACK_DEFAULT('F', "pf", &trace.trace_pgfaults, "all|maj|min",
		     "Trace pagefaults", parse_pagefaults, "maj"),
	OPT_BOOLEAN(0, "syscalls", &trace.trace_syscalls, "Trace syscalls"),
	OPT_BOOLEAN('f', "force", &trace.force, "don't complain, do it"),
	OPT_CALLBACK(0, "call-graph", &trace.opts,
		     "record_mode[,record_size]", record_callchain_help,
		     &record_parse_callchain_opt),
	OPT_BOOLEAN(0, "kernel-syscall-graph", &trace.kernel_syscallchains,
		    "Show the kernel callchains on the syscall exit path"),
	OPT_UINTEGER(0, "min-stack", &trace.min_stack,
		     "Set the minimum stack depth when parsing the callchain, "
		     "anything below the specified depth will be ignored."),
	OPT_UINTEGER(0, "max-stack", &trace.max_stack,
		     "Set the maximum stack depth when parsing the callchain, "
		     "anything beyond the specified depth will be ignored. "
		     "Default: kernel.perf_event_max_stack or " __stringify(PERF_MAX_STACK_DEPTH)),
	OPT_UINTEGER(0, "proc-map-timeout", &trace.opts.proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_END()
	};
	bool __maybe_unused max_stack_user_set = true;
	bool mmap_pages_user_set = true;
	const char * const trace_subcommands[] = { "record", NULL };
	int err;
	char bf[BUFSIZ];

	signal(SIGSEGV, sighandler_dump_stack);
	signal(SIGFPE, sighandler_dump_stack);

	trace.evlist = perf_evlist__new();
	trace.sctbl = syscalltbl__new();

	if (trace.evlist == NULL || trace.sctbl == NULL) {
		pr_err("Not enough memory to run!\n");
		err = -ENOMEM;
		goto out;
	}

	argc = parse_options_subcommand(argc, argv, trace_options, trace_subcommands,
				 trace_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	err = bpf__setup_stdout(trace.evlist);
	if (err) {
		bpf__strerror_setup_stdout(trace.evlist, err, bf, sizeof(bf));
		pr_err("ERROR: Setup BPF stdout failed: %s\n", bf);
		goto out;
	}

	err = -1;

	if (trace.trace_pgfaults) {
		trace.opts.sample_address = true;
		trace.opts.sample_time = true;
	}

	if (trace.opts.mmap_pages == UINT_MAX)
		mmap_pages_user_set = false;

	if (trace.max_stack == UINT_MAX) {
		trace.max_stack = input_name ? PERF_MAX_STACK_DEPTH : sysctl_perf_event_max_stack;
		max_stack_user_set = false;
	}

#ifdef HAVE_DWARF_UNWIND_SUPPORT
	if ((trace.min_stack || max_stack_user_set) && !callchain_param.enabled && trace.trace_syscalls)
		record_opts__parse_callchain(&trace.opts, &callchain_param, "dwarf", false);
#endif

	if (callchain_param.enabled) {
		if (!mmap_pages_user_set && geteuid() == 0)
			trace.opts.mmap_pages = perf_event_mlock_kb_in_pages() * 4;

		symbol_conf.use_callchain = true;
	}

	if (trace.evlist->nr_entries > 0)
		evlist__set_evsel_handler(trace.evlist, trace__event_handler);

	if ((argc >= 1) && (strcmp(argv[0], "record") == 0))
		return trace__record(&trace, argc-1, &argv[1]);

	/* summary_only implies summary option, but don't overwrite summary if set */
	if (trace.summary_only)
		trace.summary = trace.summary_only;

	if (!trace.trace_syscalls && !trace.trace_pgfaults &&
	    trace.evlist->nr_entries == 0 /* Was --events used? */) {
		pr_err("Please specify something to trace.\n");
		return -1;
	}

	if (!trace.trace_syscalls && ev_qualifier_str) {
		pr_err("The -e option can't be used with --no-syscalls.\n");
		goto out;
	}

	if (output_name != NULL) {
		err = trace__open_output(&trace, output_name);
		if (err < 0) {
			perror("failed to create output file");
			goto out;
		}
	}

	trace.open_id = syscalltbl__id(trace.sctbl, "open");

	if (ev_qualifier_str != NULL) {
		const char *s = ev_qualifier_str;
		struct strlist_config slist_config = {
			.dirname = system_path(STRACE_GROUPS_DIR),
		};

		trace.not_ev_qualifier = *s == '!';
		if (trace.not_ev_qualifier)
			++s;
		trace.ev_qualifier = strlist__new(s, &slist_config);
		if (trace.ev_qualifier == NULL) {
			fputs("Not enough memory to parse event qualifier",
			      trace.output);
			err = -ENOMEM;
			goto out_close;
		}

		err = trace__validate_ev_qualifier(&trace);
		if (err)
			goto out_close;
	}

	err = target__validate(&trace.opts.target);
	if (err) {
		target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	err = target__parse_uid(&trace.opts.target);
	if (err) {
		target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	if (!argc && target__none(&trace.opts.target))
		trace.opts.target.system_wide = true;

	if (input_name)
		err = trace__replay(&trace);
	else
		err = trace__run(&trace, argc, argv);

out_close:
	if (output_name != NULL)
		fclose(trace.output);
out:
	return err;
}

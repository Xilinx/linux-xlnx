#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef O_DIRECT
#define O_DIRECT	00040000
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY	00200000
#endif

#ifndef O_NOATIME
#define O_NOATIME	01000000
#endif

static size_t syscall_arg__scnprintf_open_flags(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (!(flags & O_CREAT))
		arg->mask |= 1 << (arg->idx + 1); /* Mask the mode parm */

	if (flags == 0)
		return scnprintf(bf, size, "RDONLY");
#define	P_FLAG(n) \
	if (flags & O_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~O_##n; \
	}

	P_FLAG(APPEND);
	P_FLAG(ASYNC);
	P_FLAG(CLOEXEC);
	P_FLAG(CREAT);
	P_FLAG(DIRECT);
	P_FLAG(DIRECTORY);
	P_FLAG(EXCL);
	P_FLAG(LARGEFILE);
	P_FLAG(NOATIME);
	P_FLAG(NOCTTY);
#ifdef O_NONBLOCK
	P_FLAG(NONBLOCK);
#elif O_NDELAY
	P_FLAG(NDELAY);
#endif
#ifdef O_PATH
	P_FLAG(PATH);
#endif
	P_FLAG(RDWR);
#ifdef O_DSYNC
	if ((flags & O_SYNC) == O_SYNC)
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", "SYNC");
	else {
		P_FLAG(DSYNC);
	}
#else
	P_FLAG(SYNC);
#endif
	P_FLAG(TRUNC);
	P_FLAG(WRONLY);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_OPEN_FLAGS syscall_arg__scnprintf_open_flags

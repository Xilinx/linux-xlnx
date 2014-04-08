#ifndef _ASM_S390X_S390_H
#define _ASM_S390X_S390_H

#include <linux/compat.h>
#include <linux/socket.h>
#include <linux/syscalls.h>

/* Macro that masks the high order bit of an 32 bit pointer and converts it*/
/*       to a 64 bit pointer */
#define A(__x) ((unsigned long)((__x) & 0x7FFFFFFFUL))
#define AA(__x)				\
	((unsigned long)(__x))

/* Now 32bit compatibility types */
struct ipc_kludge_32 {
        __u32   msgp;                           /* pointer              */
        __s32   msgtyp;
};

/* asm/sigcontext.h */
typedef union
{
	__u64   d;
	__u32   f; 
} freg_t32;

typedef struct
{
	unsigned int	fpc;
	unsigned int	pad;
	freg_t32	fprs[__NUM_FPRS];              
} _s390_fp_regs32;

typedef struct 
{
        __u32   mask;
        __u32	addr;
} _psw_t32 __attribute__ ((aligned(8)));

typedef struct
{
	_psw_t32	psw;
	__u32		gprs[__NUM_GPRS];
	__u32		acrs[__NUM_ACRS];
} _s390_regs_common32;

typedef struct
{
	_s390_regs_common32 regs;
	_s390_fp_regs32     fpregs;
} _sigregs32;

#define _SIGCONTEXT_NSIG32	64
#define _SIGCONTEXT_NSIG_BPW32	32
#define __SIGNAL_FRAMESIZE32	96
#define _SIGMASK_COPY_SIZE32	(sizeof(u32)*2)

struct sigcontext32
{
	__u32	oldmask[_COMPAT_NSIG_WORDS];
	__u32	sregs;				/* pointer */
};

/* asm/signal.h */

/* asm/ucontext.h */
struct ucontext32 {
	__u32			uc_flags;
	__u32			uc_link;	/* pointer */	
	compat_stack_t		uc_stack;
	_sigregs32		uc_mcontext;
	compat_sigset_t		uc_sigmask;	/* mask last for extensibility */
};

struct stat64_emu31;
struct mmap_arg_struct_emu31;
struct fadvise64_64_args;

long sys32_chown16(const char __user * filename, u16 user, u16 group);
long sys32_lchown16(const char __user * filename, u16 user, u16 group);
long sys32_fchown16(unsigned int fd, u16 user, u16 group);
long sys32_setregid16(u16 rgid, u16 egid);
long sys32_setgid16(u16 gid);
long sys32_setreuid16(u16 ruid, u16 euid);
long sys32_setuid16(u16 uid);
long sys32_setresuid16(u16 ruid, u16 euid, u16 suid);
long sys32_getresuid16(u16 __user *ruid, u16 __user *euid, u16 __user *suid);
long sys32_setresgid16(u16 rgid, u16 egid, u16 sgid);
long sys32_getresgid16(u16 __user *rgid, u16 __user *egid, u16 __user *sgid);
long sys32_setfsuid16(u16 uid);
long sys32_setfsgid16(u16 gid);
long sys32_getgroups16(int gidsetsize, u16 __user *grouplist);
long sys32_setgroups16(int gidsetsize, u16 __user *grouplist);
long sys32_getuid16(void);
long sys32_geteuid16(void);
long sys32_getgid16(void);
long sys32_getegid16(void);
long sys32_truncate64(const char __user * path, unsigned long high,
		      unsigned long low);
long sys32_ftruncate64(unsigned int fd, unsigned long high, unsigned long low);
long sys32_init_module(void __user *umod, unsigned long len,
		       const char __user *uargs);
long sys32_delete_module(const char __user *name_user, unsigned int flags);
long sys32_pread64(unsigned int fd, char __user *ubuf, size_t count,
		   u32 poshi, u32 poslo);
long sys32_pwrite64(unsigned int fd, const char __user *ubuf,
		    size_t count, u32 poshi, u32 poslo);
compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count);
long sys32_stat64(const char __user * filename, struct stat64_emu31 __user * statbuf);
long sys32_lstat64(const char __user * filename,
		   struct stat64_emu31 __user * statbuf);
long sys32_fstat64(unsigned long fd, struct stat64_emu31 __user * statbuf);
long sys32_fstatat64(unsigned int dfd, const char __user *filename,
		     struct stat64_emu31 __user* statbuf, int flag);
unsigned long old32_mmap(struct mmap_arg_struct_emu31 __user *arg);
long sys32_mmap2(struct mmap_arg_struct_emu31 __user *arg);
long sys32_read(unsigned int fd, char __user * buf, size_t count);
long sys32_write(unsigned int fd, const char __user * buf, size_t count);
long sys32_fadvise64(int fd, loff_t offset, size_t len, int advise);
long sys32_fadvise64_64(struct fadvise64_64_args __user *args);
#endif /* _ASM_S390X_S390_H */

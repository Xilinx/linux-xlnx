/*
 * arch/microblaze/kernel/sys_microblaze.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * Authors:
 *  Yasushi SHOJI <yashi@atmark-techno.com>
 *  Tetsuya OHKAWA <tetsuya@atmark-techno.com>
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/semaphore.h>

#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

long execve(const char *filename, char **argv, char **envp)
{
	struct pt_regs regs;
	int ret;

	memset(&regs, 0, sizeof(struct pt_regs));
	ret = do_execve((char *)filename, (char __user * __user *)argv,
			(char __user * __user *)envp, &regs);

	if (ret < 0)
		goto out;

	/*
	 * Save argc to the register structure for userspace.
	 */
	regs.r5 = ret; /* FIXME */

	/*
	 * We were successful.  We won't be returning to our caller, but
	 * instead to user space by manipulating the kernel stack.
	 */
	asm volatile ("addk	r5, r0, %0	\n\t"
		      "addk	r6, r0, %1	\n\t"
		      "brlid	r15, memmove	\n\t" /* copy regs to top of stack */
		      "addik	r7, r0, %2	\n\t"
		      "brid	ret_to_user	\n\t"
		      "addk	r1, r0, r3	\n\t" /* reposition stack pointer */
		      :
		      : "r" (task_pt_regs(current)),
			"r" (&regs),
			"i" (sizeof(regs))
		      : "r1", "r3", "r5", "r6", "r7", "r15", "memory");

out:
	return ret;
}
EXPORT_SYMBOL(execve);

asmlinkage int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

asmlinkage int sys_clone(int flags, unsigned long stack, struct pt_regs *regs)
{
	if (!stack) stack = regs->sp;
	return do_fork(flags, stack, regs, 0, NULL, NULL);
}

asmlinkage int sys_execve(char __user *filenamei, char __user * __user *argv,
			  char __user * __user *envp, struct pt_regs *regs)
{
	int error;
	char * filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

asmlinkage int sys_pipe(unsigned long __user *fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

static inline unsigned long
do_mmap2 (unsigned long addr, size_t len,
	 unsigned long prot, unsigned long flags,
	 unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	int ret = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (! (flags & MAP_ANONYMOUS)) {
		if (!(file = fget (fd))) {
			printk("no fd in mmap\r\n");
			goto out;
		}
	}
	
	down_write (&current->mm->mmap_sem);
	ret = do_mmap_pgoff (file, addr, len, prot, flags, pgoff);
	up_write (&current->mm->mmap_sem);
	if (file)
		fput (file);
out:
	return ret;
}

unsigned long sys_mmap2 (unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff)
{
	return do_mmap2 (addr, len, prot, flags, fd, pgoff);
}

unsigned long sys_mmap (unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
{
	int err = -EINVAL;

	if (offset & ~PAGE_MASK) {
		printk("no pagemask in mmap\r\n");
		goto out;
	}

	err = do_mmap2 (addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
out:
	return err;
}


int sys_uname (struct old_utsname * name)
{
	int err = -EFAULT;

	down_read (&uts_sem);
	if (name && !copy_to_user (name, &system_utsname, sizeof (*name)))
		err = 0;
	up_read (&uts_sem);
	return err;
}

int sys_olduname (struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok (VERIFY_WRITE, name, sizeof (struct oldold_utsname)))
		return -EFAULT;
  
	down_read (&uts_sem);
	error = __copy_to_user (&name->sysname, &system_utsname.sysname,
				__OLD_UTS_LEN);
	error -= __put_user (0, name->sysname + __OLD_UTS_LEN);
	error -= __copy_to_user (&name->nodename, &system_utsname.nodename,
				 __OLD_UTS_LEN);
	error -= __put_user (0, name->nodename + __OLD_UTS_LEN);
	error -= __copy_to_user (&name->release, &system_utsname.release,
				 __OLD_UTS_LEN);
	error -= __put_user (0, name->release + __OLD_UTS_LEN);
	error -= __copy_to_user (&name->version, &system_utsname.version,
				 __OLD_UTS_LEN);
	error -= __put_user (0, name->version + __OLD_UTS_LEN);
	error -= __copy_to_user (&name->machine, &system_utsname.machine,
				 __OLD_UTS_LEN);
	error = __put_user (0, name->machine + __OLD_UTS_LEN);
	up_read (&uts_sem);

	error = error ? -EFAULT : 0;
	return error;
}

#define pr_fmt(fmt) "kcov: " fmt

#define DISABLE_BRANCH_PROFILING
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kcov.h>

/*
 * kcov descriptor (one per opened debugfs file).
 * State transitions of the descriptor:
 *  - initial state after open()
 *  - then there must be a single ioctl(KCOV_INIT_TRACE) call
 *  - then, mmap() call (several calls are allowed but not useful)
 *  - then, repeated enable/disable for a task (only one task a time allowed)
 */
struct kcov {
	/*
	 * Reference counter. We keep one for:
	 *  - opened file descriptor
	 *  - task with enabled coverage (we can't unwire it from another task)
	 */
	atomic_t		refcount;
	/* The lock protects mode, size, area and t. */
	spinlock_t		lock;
	enum kcov_mode		mode;
	/* Size of arena (in long's for KCOV_MODE_TRACE). */
	unsigned		size;
	/* Coverage buffer shared with user space. */
	void			*area;
	/* Task for which we collect coverage, or NULL. */
	struct task_struct	*t;
};

/*
 * Entry point from instrumented code.
 * This is called once per basic-block/edge.
 */
void notrace __sanitizer_cov_trace_pc(void)
{
	struct task_struct *t;
	enum kcov_mode mode;

	t = current;
	/*
	 * We are interested in code coverage as a function of a syscall inputs,
	 * so we ignore code executed in interrupts.
	 * The checks for whether we are in an interrupt are open-coded, because
	 * 1. We can't use in_interrupt() here, since it also returns true
	 *    when we are inside local_bh_disable() section.
	 * 2. We don't want to use (in_irq() | in_serving_softirq() | in_nmi()),
	 *    since that leads to slower generated code (three separate tests,
	 *    one for each of the flags).
	 */
	if (!t || (preempt_count() & (HARDIRQ_MASK | SOFTIRQ_OFFSET
							| NMI_MASK)))
		return;
	mode = READ_ONCE(t->kcov_mode);
	if (mode == KCOV_MODE_TRACE) {
		unsigned long *area;
		unsigned long pos;

		/*
		 * There is some code that runs in interrupts but for which
		 * in_interrupt() returns false (e.g. preempt_schedule_irq()).
		 * READ_ONCE()/barrier() effectively provides load-acquire wrt
		 * interrupts, there are paired barrier()/WRITE_ONCE() in
		 * kcov_ioctl_locked().
		 */
		barrier();
		area = t->kcov_area;
		/* The first word is number of subsequent PCs. */
		pos = READ_ONCE(area[0]) + 1;
		if (likely(pos < t->kcov_size)) {
			area[pos] = _RET_IP_;
			WRITE_ONCE(area[0], pos);
		}
	}
}
EXPORT_SYMBOL(__sanitizer_cov_trace_pc);

static void kcov_get(struct kcov *kcov)
{
	atomic_inc(&kcov->refcount);
}

static void kcov_put(struct kcov *kcov)
{
	if (atomic_dec_and_test(&kcov->refcount)) {
		vfree(kcov->area);
		kfree(kcov);
	}
}

void kcov_task_init(struct task_struct *t)
{
	t->kcov_mode = KCOV_MODE_DISABLED;
	t->kcov_size = 0;
	t->kcov_area = NULL;
	t->kcov = NULL;
}

void kcov_task_exit(struct task_struct *t)
{
	struct kcov *kcov;

	kcov = t->kcov;
	if (kcov == NULL)
		return;
	spin_lock(&kcov->lock);
	if (WARN_ON(kcov->t != t)) {
		spin_unlock(&kcov->lock);
		return;
	}
	/* Just to not leave dangling references behind. */
	kcov_task_init(t);
	kcov->t = NULL;
	spin_unlock(&kcov->lock);
	kcov_put(kcov);
}

static int kcov_mmap(struct file *filep, struct vm_area_struct *vma)
{
	int res = 0;
	void *area;
	struct kcov *kcov = vma->vm_file->private_data;
	unsigned long size, off;
	struct page *page;

	area = vmalloc_user(vma->vm_end - vma->vm_start);
	if (!area)
		return -ENOMEM;

	spin_lock(&kcov->lock);
	size = kcov->size * sizeof(unsigned long);
	if (kcov->mode == KCOV_MODE_DISABLED || vma->vm_pgoff != 0 ||
	    vma->vm_end - vma->vm_start != size) {
		res = -EINVAL;
		goto exit;
	}
	if (!kcov->area) {
		kcov->area = area;
		vma->vm_flags |= VM_DONTEXPAND;
		spin_unlock(&kcov->lock);
		for (off = 0; off < size; off += PAGE_SIZE) {
			page = vmalloc_to_page(kcov->area + off);
			if (vm_insert_page(vma, vma->vm_start + off, page))
				WARN_ONCE(1, "vm_insert_page() failed");
		}
		return 0;
	}
exit:
	spin_unlock(&kcov->lock);
	vfree(area);
	return res;
}

static int kcov_open(struct inode *inode, struct file *filep)
{
	struct kcov *kcov;

	kcov = kzalloc(sizeof(*kcov), GFP_KERNEL);
	if (!kcov)
		return -ENOMEM;
	atomic_set(&kcov->refcount, 1);
	spin_lock_init(&kcov->lock);
	filep->private_data = kcov;
	return nonseekable_open(inode, filep);
}

static int kcov_close(struct inode *inode, struct file *filep)
{
	kcov_put(filep->private_data);
	return 0;
}

static int kcov_ioctl_locked(struct kcov *kcov, unsigned int cmd,
			     unsigned long arg)
{
	struct task_struct *t;
	unsigned long size, unused;

	switch (cmd) {
	case KCOV_INIT_TRACE:
		/*
		 * Enable kcov in trace mode and setup buffer size.
		 * Must happen before anything else.
		 */
		if (kcov->mode != KCOV_MODE_DISABLED)
			return -EBUSY;
		/*
		 * Size must be at least 2 to hold current position and one PC.
		 * Later we allocate size * sizeof(unsigned long) memory,
		 * that must not overflow.
		 */
		size = arg;
		if (size < 2 || size > INT_MAX / sizeof(unsigned long))
			return -EINVAL;
		kcov->size = size;
		kcov->mode = KCOV_MODE_TRACE;
		return 0;
	case KCOV_ENABLE:
		/*
		 * Enable coverage for the current task.
		 * At this point user must have been enabled trace mode,
		 * and mmapped the file. Coverage collection is disabled only
		 * at task exit or voluntary by KCOV_DISABLE. After that it can
		 * be enabled for another task.
		 */
		unused = arg;
		if (unused != 0 || kcov->mode == KCOV_MODE_DISABLED ||
		    kcov->area == NULL)
			return -EINVAL;
		if (kcov->t != NULL)
			return -EBUSY;
		t = current;
		/* Cache in task struct for performance. */
		t->kcov_size = kcov->size;
		t->kcov_area = kcov->area;
		/* See comment in __sanitizer_cov_trace_pc(). */
		barrier();
		WRITE_ONCE(t->kcov_mode, kcov->mode);
		t->kcov = kcov;
		kcov->t = t;
		/* This is put either in kcov_task_exit() or in KCOV_DISABLE. */
		kcov_get(kcov);
		return 0;
	case KCOV_DISABLE:
		/* Disable coverage for the current task. */
		unused = arg;
		if (unused != 0 || current->kcov != kcov)
			return -EINVAL;
		t = current;
		if (WARN_ON(kcov->t != t))
			return -EINVAL;
		kcov_task_init(t);
		kcov->t = NULL;
		kcov_put(kcov);
		return 0;
	default:
		return -ENOTTY;
	}
}

static long kcov_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct kcov *kcov;
	int res;

	kcov = filep->private_data;
	spin_lock(&kcov->lock);
	res = kcov_ioctl_locked(kcov, cmd, arg);
	spin_unlock(&kcov->lock);
	return res;
}

static const struct file_operations kcov_fops = {
	.open		= kcov_open,
	.unlocked_ioctl	= kcov_ioctl,
	.mmap		= kcov_mmap,
	.release        = kcov_close,
};

static int __init kcov_init(void)
{
	/*
	 * The kcov debugfs file won't ever get removed and thus,
	 * there is no need to protect it against removal races. The
	 * use of debugfs_create_file_unsafe() is actually safe here.
	 */
	if (!debugfs_create_file_unsafe("kcov", 0600, NULL, NULL, &kcov_fops)) {
		pr_err("failed to create kcov in debugfs\n");
		return -ENOMEM;
	}
	return 0;
}

device_initcall(kcov_init);

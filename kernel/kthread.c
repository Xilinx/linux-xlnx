/* Kernel thread helper functions.
 *   Copyright (C) 2004 IBM Corporation, Rusty Russell.
 *
 * Creation is done via kthreadd, so that we get a clean environment
 * even if we're invoked from userspace (think modprobe, hotplug cpu,
 * etc.).
 */
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/cpuset.h>
#include <linux/unistd.h>
#include <linux/file.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/freezer.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <trace/events/sched.h>

static DEFINE_SPINLOCK(kthread_create_lock);
static LIST_HEAD(kthread_create_list);
struct task_struct *kthreadd_task;

struct kthread_create_info
{
	/* Information passed to kthread() from kthreadd. */
	int (*threadfn)(void *data);
	void *data;
	int node;

	/* Result passed back to kthread_create() from kthreadd. */
	struct task_struct *result;
	struct completion *done;

	struct list_head list;
};

struct kthread {
	unsigned long flags;
	unsigned int cpu;
	void *data;
	struct completion parked;
	struct completion exited;
};

enum KTHREAD_BITS {
	KTHREAD_IS_PER_CPU = 0,
	KTHREAD_SHOULD_STOP,
	KTHREAD_SHOULD_PARK,
	KTHREAD_IS_PARKED,
};

#define __to_kthread(vfork)	\
	container_of(vfork, struct kthread, exited)

static inline struct kthread *to_kthread(struct task_struct *k)
{
	return __to_kthread(k->vfork_done);
}

static struct kthread *to_live_kthread(struct task_struct *k)
{
	struct completion *vfork = ACCESS_ONCE(k->vfork_done);
	if (likely(vfork) && try_get_task_stack(k))
		return __to_kthread(vfork);
	return NULL;
}

/**
 * kthread_should_stop - should this kthread return now?
 *
 * When someone calls kthread_stop() on your kthread, it will be woken
 * and this will return true.  You should then return, and your return
 * value will be passed through to kthread_stop().
 */
bool kthread_should_stop(void)
{
	return test_bit(KTHREAD_SHOULD_STOP, &to_kthread(current)->flags);
}
EXPORT_SYMBOL(kthread_should_stop);

/**
 * kthread_should_park - should this kthread park now?
 *
 * When someone calls kthread_park() on your kthread, it will be woken
 * and this will return true.  You should then do the necessary
 * cleanup and call kthread_parkme()
 *
 * Similar to kthread_should_stop(), but this keeps the thread alive
 * and in a park position. kthread_unpark() "restarts" the thread and
 * calls the thread function again.
 */
bool kthread_should_park(void)
{
	return test_bit(KTHREAD_SHOULD_PARK, &to_kthread(current)->flags);
}
EXPORT_SYMBOL_GPL(kthread_should_park);

/**
 * kthread_freezable_should_stop - should this freezable kthread return now?
 * @was_frozen: optional out parameter, indicates whether %current was frozen
 *
 * kthread_should_stop() for freezable kthreads, which will enter
 * refrigerator if necessary.  This function is safe from kthread_stop() /
 * freezer deadlock and freezable kthreads should use this function instead
 * of calling try_to_freeze() directly.
 */
bool kthread_freezable_should_stop(bool *was_frozen)
{
	bool frozen = false;

	might_sleep();

	if (unlikely(freezing(current)))
		frozen = __refrigerator(true);

	if (was_frozen)
		*was_frozen = frozen;

	return kthread_should_stop();
}
EXPORT_SYMBOL_GPL(kthread_freezable_should_stop);

/**
 * kthread_data - return data value specified on kthread creation
 * @task: kthread task in question
 *
 * Return the data value specified when kthread @task was created.
 * The caller is responsible for ensuring the validity of @task when
 * calling this function.
 */
void *kthread_data(struct task_struct *task)
{
	return to_kthread(task)->data;
}

/**
 * kthread_probe_data - speculative version of kthread_data()
 * @task: possible kthread task in question
 *
 * @task could be a kthread task.  Return the data value specified when it
 * was created if accessible.  If @task isn't a kthread task or its data is
 * inaccessible for any reason, %NULL is returned.  This function requires
 * that @task itself is safe to dereference.
 */
void *kthread_probe_data(struct task_struct *task)
{
	struct kthread *kthread = to_kthread(task);
	void *data = NULL;

	probe_kernel_read(&data, &kthread->data, sizeof(data));
	return data;
}

static void __kthread_parkme(struct kthread *self)
{
	__set_current_state(TASK_PARKED);
	while (test_bit(KTHREAD_SHOULD_PARK, &self->flags)) {
		if (!test_and_set_bit(KTHREAD_IS_PARKED, &self->flags))
			complete(&self->parked);
		schedule();
		__set_current_state(TASK_PARKED);
	}
	clear_bit(KTHREAD_IS_PARKED, &self->flags);
	__set_current_state(TASK_RUNNING);
}

void kthread_parkme(void)
{
	__kthread_parkme(to_kthread(current));
}
EXPORT_SYMBOL_GPL(kthread_parkme);

static int kthread(void *_create)
{
	/* Copy data: it's on kthread's stack */
	struct kthread_create_info *create = _create;
	int (*threadfn)(void *data) = create->threadfn;
	void *data = create->data;
	struct completion *done;
	struct kthread self;
	int ret;

	self.flags = 0;
	self.data = data;
	init_completion(&self.exited);
	init_completion(&self.parked);
	current->vfork_done = &self.exited;

	/* If user was SIGKILLed, I release the structure. */
	done = xchg(&create->done, NULL);
	if (!done) {
		kfree(create);
		do_exit(-EINTR);
	}
	/* OK, tell user we're spawned, wait for stop or wakeup */
	__set_current_state(TASK_UNINTERRUPTIBLE);
	create->result = current;
	complete(done);
	schedule();

	ret = -EINTR;

	if (!test_bit(KTHREAD_SHOULD_STOP, &self.flags)) {
		__kthread_parkme(&self);
		ret = threadfn(data);
	}
	/* we can't just return, we must preserve "self" on stack */
	do_exit(ret);
}

/* called from do_fork() to get node information for about to be created task */
int tsk_fork_get_node(struct task_struct *tsk)
{
#ifdef CONFIG_NUMA
	if (tsk == kthreadd_task)
		return tsk->pref_node_fork;
#endif
	return NUMA_NO_NODE;
}

static void create_kthread(struct kthread_create_info *create)
{
	int pid;

#ifdef CONFIG_NUMA
	current->pref_node_fork = create->node;
#endif
	/* We want our own signal handler (we take no signals by default). */
	pid = kernel_thread(kthread, create, CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) {
		/* If user was SIGKILLed, I release the structure. */
		struct completion *done = xchg(&create->done, NULL);

		if (!done) {
			kfree(create);
			return;
		}
		create->result = ERR_PTR(pid);
		complete(done);
	}
}

static struct task_struct *__kthread_create_on_node(int (*threadfn)(void *data),
						    void *data, int node,
						    const char namefmt[],
						    va_list args)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct task_struct *task;
	struct kthread_create_info *create = kmalloc(sizeof(*create),
						     GFP_KERNEL);

	if (!create)
		return ERR_PTR(-ENOMEM);
	create->threadfn = threadfn;
	create->data = data;
	create->node = node;
	create->done = &done;

	spin_lock(&kthread_create_lock);
	list_add_tail(&create->list, &kthread_create_list);
	spin_unlock(&kthread_create_lock);

	wake_up_process(kthreadd_task);
	/*
	 * Wait for completion in killable state, for I might be chosen by
	 * the OOM killer while kthreadd is trying to allocate memory for
	 * new kernel thread.
	 */
	if (unlikely(wait_for_completion_killable(&done))) {
		/*
		 * If I was SIGKILLed before kthreadd (or new kernel thread)
		 * calls complete(), leave the cleanup of this structure to
		 * that thread.
		 */
		if (xchg(&create->done, NULL))
			return ERR_PTR(-EINTR);
		/*
		 * kthreadd (or new kernel thread) will call complete()
		 * shortly.
		 */
		wait_for_completion(&done);
	}
	task = create->result;
	if (!IS_ERR(task)) {
		static const struct sched_param param = { .sched_priority = 0 };

		vsnprintf(task->comm, sizeof(task->comm), namefmt, args);
		/*
		 * root may have changed our (kthreadd's) priority or CPU mask.
		 * The kernel thread should not inherit these properties.
		 */
		sched_setscheduler_nocheck(task, SCHED_NORMAL, &param);
		set_cpus_allowed_ptr(task, cpu_all_mask);
	}
	kfree(create);
	return task;
}

/**
 * kthread_create_on_node - create a kthread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @node: task and thread structures for the thread are allocated on this node
 * @namefmt: printf-style name for the thread.
 *
 * Description: This helper function creates and names a kernel
 * thread.  The thread will be stopped: use wake_up_process() to start
 * it.  See also kthread_run().  The new thread has SCHED_NORMAL policy and
 * is affine to all CPUs.
 *
 * If thread is going to be bound on a particular cpu, give its node
 * in @node, to get NUMA affinity for kthread stack, or else give NUMA_NO_NODE.
 * When woken, the thread will run @threadfn() with @data as its
 * argument. @threadfn() can either call do_exit() directly if it is a
 * standalone thread for which no one will call kthread_stop(), or
 * return when 'kthread_should_stop()' is true (which means
 * kthread_stop() has been called).  The return value should be zero
 * or a negative error number; it will be passed to kthread_stop().
 *
 * Returns a task_struct or ERR_PTR(-ENOMEM) or ERR_PTR(-EINTR).
 */
struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data, int node,
					   const char namefmt[],
					   ...)
{
	struct task_struct *task;
	va_list args;

	va_start(args, namefmt);
	task = __kthread_create_on_node(threadfn, data, node, namefmt, args);
	va_end(args);

	return task;
}
EXPORT_SYMBOL(kthread_create_on_node);

static void __kthread_bind_mask(struct task_struct *p, const struct cpumask *mask, long state)
{
	unsigned long flags;

	if (!wait_task_inactive(p, state)) {
		WARN_ON(1);
		return;
	}

	/* It's safe because the task is inactive. */
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	do_set_cpus_allowed(p, mask);
	p->flags |= PF_NO_SETAFFINITY;
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);
}

static void __kthread_bind(struct task_struct *p, unsigned int cpu, long state)
{
	__kthread_bind_mask(p, cpumask_of(cpu), state);
}

void kthread_bind_mask(struct task_struct *p, const struct cpumask *mask)
{
	__kthread_bind_mask(p, mask, TASK_UNINTERRUPTIBLE);
}

/**
 * kthread_bind - bind a just-created kthread to a cpu.
 * @p: thread created by kthread_create().
 * @cpu: cpu (might not be online, must be possible) for @k to run on.
 *
 * Description: This function is equivalent to set_cpus_allowed(),
 * except that @cpu doesn't need to be online, and the thread must be
 * stopped (i.e., just returned from kthread_create()).
 */
void kthread_bind(struct task_struct *p, unsigned int cpu)
{
	__kthread_bind(p, cpu, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(kthread_bind);

/**
 * kthread_create_on_cpu - Create a cpu bound kthread
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @cpu: The cpu on which the thread should be bound,
 * @namefmt: printf-style name for the thread. Format is restricted
 *	     to "name.*%u". Code fills in cpu number.
 *
 * Description: This helper function creates and names a kernel thread
 * The thread will be woken and put into park mode.
 */
struct task_struct *kthread_create_on_cpu(int (*threadfn)(void *data),
					  void *data, unsigned int cpu,
					  const char *namefmt)
{
	struct task_struct *p;

	p = kthread_create_on_node(threadfn, data, cpu_to_node(cpu), namefmt,
				   cpu);
	if (IS_ERR(p))
		return p;
	kthread_bind(p, cpu);
	/* CPU hotplug need to bind once again when unparking the thread. */
	set_bit(KTHREAD_IS_PER_CPU, &to_kthread(p)->flags);
	to_kthread(p)->cpu = cpu;
	return p;
}

static void __kthread_unpark(struct task_struct *k, struct kthread *kthread)
{
	clear_bit(KTHREAD_SHOULD_PARK, &kthread->flags);
	/*
	 * We clear the IS_PARKED bit here as we don't wait
	 * until the task has left the park code. So if we'd
	 * park before that happens we'd see the IS_PARKED bit
	 * which might be about to be cleared.
	 */
	if (test_and_clear_bit(KTHREAD_IS_PARKED, &kthread->flags)) {
		/*
		 * Newly created kthread was parked when the CPU was offline.
		 * The binding was lost and we need to set it again.
		 */
		if (test_bit(KTHREAD_IS_PER_CPU, &kthread->flags))
			__kthread_bind(k, kthread->cpu, TASK_PARKED);
		wake_up_state(k, TASK_PARKED);
	}
}

/**
 * kthread_unpark - unpark a thread created by kthread_create().
 * @k:		thread created by kthread_create().
 *
 * Sets kthread_should_park() for @k to return false, wakes it, and
 * waits for it to return. If the thread is marked percpu then its
 * bound to the cpu again.
 */
void kthread_unpark(struct task_struct *k)
{
	struct kthread *kthread = to_live_kthread(k);

	if (kthread) {
		__kthread_unpark(k, kthread);
		put_task_stack(k);
	}
}
EXPORT_SYMBOL_GPL(kthread_unpark);

/**
 * kthread_park - park a thread created by kthread_create().
 * @k: thread created by kthread_create().
 *
 * Sets kthread_should_park() for @k to return true, wakes it, and
 * waits for it to return. This can also be called after kthread_create()
 * instead of calling wake_up_process(): the thread will park without
 * calling threadfn().
 *
 * Returns 0 if the thread is parked, -ENOSYS if the thread exited.
 * If called by the kthread itself just the park bit is set.
 */
int kthread_park(struct task_struct *k)
{
	struct kthread *kthread = to_live_kthread(k);
	int ret = -ENOSYS;

	if (kthread) {
		if (!test_bit(KTHREAD_IS_PARKED, &kthread->flags)) {
			set_bit(KTHREAD_SHOULD_PARK, &kthread->flags);
			if (k != current) {
				wake_up_process(k);
				wait_for_completion(&kthread->parked);
			}
		}
		put_task_stack(k);
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(kthread_park);

/**
 * kthread_stop - stop a thread created by kthread_create().
 * @k: thread created by kthread_create().
 *
 * Sets kthread_should_stop() for @k to return true, wakes it, and
 * waits for it to exit. This can also be called after kthread_create()
 * instead of calling wake_up_process(): the thread will exit without
 * calling threadfn().
 *
 * If threadfn() may call do_exit() itself, the caller must ensure
 * task_struct can't go away.
 *
 * Returns the result of threadfn(), or %-EINTR if wake_up_process()
 * was never called.
 */
int kthread_stop(struct task_struct *k)
{
	struct kthread *kthread;
	int ret;

	trace_sched_kthread_stop(k);

	get_task_struct(k);
	kthread = to_live_kthread(k);
	if (kthread) {
		set_bit(KTHREAD_SHOULD_STOP, &kthread->flags);
		__kthread_unpark(k, kthread);
		wake_up_process(k);
		wait_for_completion(&kthread->exited);
		put_task_stack(k);
	}
	ret = k->exit_code;
	put_task_struct(k);

	trace_sched_kthread_stop_ret(ret);
	return ret;
}
EXPORT_SYMBOL(kthread_stop);

int kthreadd(void *unused)
{
	struct task_struct *tsk = current;

	/* Setup a clean context for our children to inherit. */
	set_task_comm(tsk, "kthreadd");
	ignore_signals(tsk);
	set_cpus_allowed_ptr(tsk, cpu_all_mask);
	set_mems_allowed(node_states[N_MEMORY]);

	current->flags |= PF_NOFREEZE;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&kthread_create_list))
			schedule();
		__set_current_state(TASK_RUNNING);

		spin_lock(&kthread_create_lock);
		while (!list_empty(&kthread_create_list)) {
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next,
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			create_kthread(create);

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}

void __kthread_init_worker(struct kthread_worker *worker,
				const char *name,
				struct lock_class_key *key)
{
	memset(worker, 0, sizeof(struct kthread_worker));
	spin_lock_init(&worker->lock);
	lockdep_set_class_and_name(&worker->lock, key, name);
	INIT_LIST_HEAD(&worker->work_list);
	INIT_LIST_HEAD(&worker->delayed_work_list);
}
EXPORT_SYMBOL_GPL(__kthread_init_worker);

/**
 * kthread_worker_fn - kthread function to process kthread_worker
 * @worker_ptr: pointer to initialized kthread_worker
 *
 * This function implements the main cycle of kthread worker. It processes
 * work_list until it is stopped with kthread_stop(). It sleeps when the queue
 * is empty.
 *
 * The works are not allowed to keep any locks, disable preemption or interrupts
 * when they finish. There is defined a safe point for freezing when one work
 * finishes and before a new one is started.
 *
 * Also the works must not be handled by more than one worker at the same time,
 * see also kthread_queue_work().
 */
int kthread_worker_fn(void *worker_ptr)
{
	struct kthread_worker *worker = worker_ptr;
	struct kthread_work *work;

	/*
	 * FIXME: Update the check and remove the assignment when all kthread
	 * worker users are created using kthread_create_worker*() functions.
	 */
	WARN_ON(worker->task && worker->task != current);
	worker->task = current;

	if (worker->flags & KTW_FREEZABLE)
		set_freezable();

repeat:
	set_current_state(TASK_INTERRUPTIBLE);	/* mb paired w/ kthread_stop */

	if (kthread_should_stop()) {
		__set_current_state(TASK_RUNNING);
		spin_lock_irq(&worker->lock);
		worker->task = NULL;
		spin_unlock_irq(&worker->lock);
		return 0;
	}

	work = NULL;
	spin_lock_irq(&worker->lock);
	if (!list_empty(&worker->work_list)) {
		work = list_first_entry(&worker->work_list,
					struct kthread_work, node);
		list_del_init(&work->node);
	}
	worker->current_work = work;
	spin_unlock_irq(&worker->lock);

	if (work) {
		__set_current_state(TASK_RUNNING);
		work->func(work);
	} else if (!freezing(current))
		schedule();

	try_to_freeze();
	goto repeat;
}
EXPORT_SYMBOL_GPL(kthread_worker_fn);

static struct kthread_worker *
__kthread_create_worker(int cpu, unsigned int flags,
			const char namefmt[], va_list args)
{
	struct kthread_worker *worker;
	struct task_struct *task;

	worker = kzalloc(sizeof(*worker), GFP_KERNEL);
	if (!worker)
		return ERR_PTR(-ENOMEM);

	kthread_init_worker(worker);

	if (cpu >= 0) {
		char name[TASK_COMM_LEN];

		/*
		 * kthread_create_worker_on_cpu() allows to pass a generic
		 * namefmt in compare with kthread_create_on_cpu. We need
		 * to format it here.
		 */
		vsnprintf(name, sizeof(name), namefmt, args);
		task = kthread_create_on_cpu(kthread_worker_fn, worker,
					     cpu, name);
	} else {
		task = __kthread_create_on_node(kthread_worker_fn, worker,
						-1, namefmt, args);
	}

	if (IS_ERR(task))
		goto fail_task;

	worker->flags = flags;
	worker->task = task;
	wake_up_process(task);
	return worker;

fail_task:
	kfree(worker);
	return ERR_CAST(task);
}

/**
 * kthread_create_worker - create a kthread worker
 * @flags: flags modifying the default behavior of the worker
 * @namefmt: printf-style name for the kthread worker (task).
 *
 * Returns a pointer to the allocated worker on success, ERR_PTR(-ENOMEM)
 * when the needed structures could not get allocated, and ERR_PTR(-EINTR)
 * when the worker was SIGKILLed.
 */
struct kthread_worker *
kthread_create_worker(unsigned int flags, const char namefmt[], ...)
{
	struct kthread_worker *worker;
	va_list args;

	va_start(args, namefmt);
	worker = __kthread_create_worker(-1, flags, namefmt, args);
	va_end(args);

	return worker;
}
EXPORT_SYMBOL(kthread_create_worker);

/**
 * kthread_create_worker_on_cpu - create a kthread worker and bind it
 *	it to a given CPU and the associated NUMA node.
 * @cpu: CPU number
 * @flags: flags modifying the default behavior of the worker
 * @namefmt: printf-style name for the kthread worker (task).
 *
 * Use a valid CPU number if you want to bind the kthread worker
 * to the given CPU and the associated NUMA node.
 *
 * A good practice is to add the cpu number also into the worker name.
 * For example, use kthread_create_worker_on_cpu(cpu, "helper/%d", cpu).
 *
 * Returns a pointer to the allocated worker on success, ERR_PTR(-ENOMEM)
 * when the needed structures could not get allocated, and ERR_PTR(-EINTR)
 * when the worker was SIGKILLed.
 */
struct kthread_worker *
kthread_create_worker_on_cpu(int cpu, unsigned int flags,
			     const char namefmt[], ...)
{
	struct kthread_worker *worker;
	va_list args;

	va_start(args, namefmt);
	worker = __kthread_create_worker(cpu, flags, namefmt, args);
	va_end(args);

	return worker;
}
EXPORT_SYMBOL(kthread_create_worker_on_cpu);

/*
 * Returns true when the work could not be queued at the moment.
 * It happens when it is already pending in a worker list
 * or when it is being cancelled.
 */
static inline bool queuing_blocked(struct kthread_worker *worker,
				   struct kthread_work *work)
{
	lockdep_assert_held(&worker->lock);

	return !list_empty(&work->node) || work->canceling;
}

static void kthread_insert_work_sanity_check(struct kthread_worker *worker,
					     struct kthread_work *work)
{
	lockdep_assert_held(&worker->lock);
	WARN_ON_ONCE(!list_empty(&work->node));
	/* Do not use a work with >1 worker, see kthread_queue_work() */
	WARN_ON_ONCE(work->worker && work->worker != worker);
}

/* insert @work before @pos in @worker */
static void kthread_insert_work(struct kthread_worker *worker,
				struct kthread_work *work,
				struct list_head *pos)
{
	kthread_insert_work_sanity_check(worker, work);

	list_add_tail(&work->node, pos);
	work->worker = worker;
	if (!worker->current_work && likely(worker->task))
		wake_up_process(worker->task);
}

/**
 * kthread_queue_work - queue a kthread_work
 * @worker: target kthread_worker
 * @work: kthread_work to queue
 *
 * Queue @work to work processor @task for async execution.  @task
 * must have been created with kthread_worker_create().  Returns %true
 * if @work was successfully queued, %false if it was already pending.
 *
 * Reinitialize the work if it needs to be used by another worker.
 * For example, when the worker was stopped and started again.
 */
bool kthread_queue_work(struct kthread_worker *worker,
			struct kthread_work *work)
{
	bool ret = false;
	unsigned long flags;

	spin_lock_irqsave(&worker->lock, flags);
	if (!queuing_blocked(worker, work)) {
		kthread_insert_work(worker, work, &worker->work_list);
		ret = true;
	}
	spin_unlock_irqrestore(&worker->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kthread_queue_work);

/**
 * kthread_delayed_work_timer_fn - callback that queues the associated kthread
 *	delayed work when the timer expires.
 * @__data: pointer to the data associated with the timer
 *
 * The format of the function is defined by struct timer_list.
 * It should have been called from irqsafe timer with irq already off.
 */
void kthread_delayed_work_timer_fn(unsigned long __data)
{
	struct kthread_delayed_work *dwork =
		(struct kthread_delayed_work *)__data;
	struct kthread_work *work = &dwork->work;
	struct kthread_worker *worker = work->worker;

	/*
	 * This might happen when a pending work is reinitialized.
	 * It means that it is used a wrong way.
	 */
	if (WARN_ON_ONCE(!worker))
		return;

	spin_lock(&worker->lock);
	/* Work must not be used with >1 worker, see kthread_queue_work(). */
	WARN_ON_ONCE(work->worker != worker);

	/* Move the work from worker->delayed_work_list. */
	WARN_ON_ONCE(list_empty(&work->node));
	list_del_init(&work->node);
	kthread_insert_work(worker, work, &worker->work_list);

	spin_unlock(&worker->lock);
}
EXPORT_SYMBOL(kthread_delayed_work_timer_fn);

void __kthread_queue_delayed_work(struct kthread_worker *worker,
				  struct kthread_delayed_work *dwork,
				  unsigned long delay)
{
	struct timer_list *timer = &dwork->timer;
	struct kthread_work *work = &dwork->work;

	WARN_ON_ONCE(timer->function != kthread_delayed_work_timer_fn ||
		     timer->data != (unsigned long)dwork);

	/*
	 * If @delay is 0, queue @dwork->work immediately.  This is for
	 * both optimization and correctness.  The earliest @timer can
	 * expire is on the closest next tick and delayed_work users depend
	 * on that there's no such delay when @delay is 0.
	 */
	if (!delay) {
		kthread_insert_work(worker, work, &worker->work_list);
		return;
	}

	/* Be paranoid and try to detect possible races already now. */
	kthread_insert_work_sanity_check(worker, work);

	list_add(&work->node, &worker->delayed_work_list);
	work->worker = worker;
	timer_stats_timer_set_start_info(&dwork->timer);
	timer->expires = jiffies + delay;
	add_timer(timer);
}

/**
 * kthread_queue_delayed_work - queue the associated kthread work
 *	after a delay.
 * @worker: target kthread_worker
 * @dwork: kthread_delayed_work to queue
 * @delay: number of jiffies to wait before queuing
 *
 * If the work has not been pending it starts a timer that will queue
 * the work after the given @delay. If @delay is zero, it queues the
 * work immediately.
 *
 * Return: %false if the @work has already been pending. It means that
 * either the timer was running or the work was queued. It returns %true
 * otherwise.
 */
bool kthread_queue_delayed_work(struct kthread_worker *worker,
				struct kthread_delayed_work *dwork,
				unsigned long delay)
{
	struct kthread_work *work = &dwork->work;
	unsigned long flags;
	bool ret = false;

	spin_lock_irqsave(&worker->lock, flags);

	if (!queuing_blocked(worker, work)) {
		__kthread_queue_delayed_work(worker, dwork, delay);
		ret = true;
	}

	spin_unlock_irqrestore(&worker->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kthread_queue_delayed_work);

struct kthread_flush_work {
	struct kthread_work	work;
	struct completion	done;
};

static void kthread_flush_work_fn(struct kthread_work *work)
{
	struct kthread_flush_work *fwork =
		container_of(work, struct kthread_flush_work, work);
	complete(&fwork->done);
}

/**
 * kthread_flush_work - flush a kthread_work
 * @work: work to flush
 *
 * If @work is queued or executing, wait for it to finish execution.
 */
void kthread_flush_work(struct kthread_work *work)
{
	struct kthread_flush_work fwork = {
		KTHREAD_WORK_INIT(fwork.work, kthread_flush_work_fn),
		COMPLETION_INITIALIZER_ONSTACK(fwork.done),
	};
	struct kthread_worker *worker;
	bool noop = false;

	worker = work->worker;
	if (!worker)
		return;

	spin_lock_irq(&worker->lock);
	/* Work must not be used with >1 worker, see kthread_queue_work(). */
	WARN_ON_ONCE(work->worker != worker);

	if (!list_empty(&work->node))
		kthread_insert_work(worker, &fwork.work, work->node.next);
	else if (worker->current_work == work)
		kthread_insert_work(worker, &fwork.work,
				    worker->work_list.next);
	else
		noop = true;

	spin_unlock_irq(&worker->lock);

	if (!noop)
		wait_for_completion(&fwork.done);
}
EXPORT_SYMBOL_GPL(kthread_flush_work);

/*
 * This function removes the work from the worker queue. Also it makes sure
 * that it won't get queued later via the delayed work's timer.
 *
 * The work might still be in use when this function finishes. See the
 * current_work proceed by the worker.
 *
 * Return: %true if @work was pending and successfully canceled,
 *	%false if @work was not pending
 */
static bool __kthread_cancel_work(struct kthread_work *work, bool is_dwork,
				  unsigned long *flags)
{
	/* Try to cancel the timer if exists. */
	if (is_dwork) {
		struct kthread_delayed_work *dwork =
			container_of(work, struct kthread_delayed_work, work);
		struct kthread_worker *worker = work->worker;

		/*
		 * del_timer_sync() must be called to make sure that the timer
		 * callback is not running. The lock must be temporary released
		 * to avoid a deadlock with the callback. In the meantime,
		 * any queuing is blocked by setting the canceling counter.
		 */
		work->canceling++;
		spin_unlock_irqrestore(&worker->lock, *flags);
		del_timer_sync(&dwork->timer);
		spin_lock_irqsave(&worker->lock, *flags);
		work->canceling--;
	}

	/*
	 * Try to remove the work from a worker list. It might either
	 * be from worker->work_list or from worker->delayed_work_list.
	 */
	if (!list_empty(&work->node)) {
		list_del_init(&work->node);
		return true;
	}

	return false;
}

/**
 * kthread_mod_delayed_work - modify delay of or queue a kthread delayed work
 * @worker: kthread worker to use
 * @dwork: kthread delayed work to queue
 * @delay: number of jiffies to wait before queuing
 *
 * If @dwork is idle, equivalent to kthread_queue_delayed_work(). Otherwise,
 * modify @dwork's timer so that it expires after @delay. If @delay is zero,
 * @work is guaranteed to be queued immediately.
 *
 * Return: %true if @dwork was pending and its timer was modified,
 * %false otherwise.
 *
 * A special case is when the work is being canceled in parallel.
 * It might be caused either by the real kthread_cancel_delayed_work_sync()
 * or yet another kthread_mod_delayed_work() call. We let the other command
 * win and return %false here. The caller is supposed to synchronize these
 * operations a reasonable way.
 *
 * This function is safe to call from any context including IRQ handler.
 * See __kthread_cancel_work() and kthread_delayed_work_timer_fn()
 * for details.
 */
bool kthread_mod_delayed_work(struct kthread_worker *worker,
			      struct kthread_delayed_work *dwork,
			      unsigned long delay)
{
	struct kthread_work *work = &dwork->work;
	unsigned long flags;
	int ret = false;

	spin_lock_irqsave(&worker->lock, flags);

	/* Do not bother with canceling when never queued. */
	if (!work->worker)
		goto fast_queue;

	/* Work must not be used with >1 worker, see kthread_queue_work() */
	WARN_ON_ONCE(work->worker != worker);

	/* Do not fight with another command that is canceling this work. */
	if (work->canceling)
		goto out;

	ret = __kthread_cancel_work(work, true, &flags);
fast_queue:
	__kthread_queue_delayed_work(worker, dwork, delay);
out:
	spin_unlock_irqrestore(&worker->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kthread_mod_delayed_work);

static bool __kthread_cancel_work_sync(struct kthread_work *work, bool is_dwork)
{
	struct kthread_worker *worker = work->worker;
	unsigned long flags;
	int ret = false;

	if (!worker)
		goto out;

	spin_lock_irqsave(&worker->lock, flags);
	/* Work must not be used with >1 worker, see kthread_queue_work(). */
	WARN_ON_ONCE(work->worker != worker);

	ret = __kthread_cancel_work(work, is_dwork, &flags);

	if (worker->current_work != work)
		goto out_fast;

	/*
	 * The work is in progress and we need to wait with the lock released.
	 * In the meantime, block any queuing by setting the canceling counter.
	 */
	work->canceling++;
	spin_unlock_irqrestore(&worker->lock, flags);
	kthread_flush_work(work);
	spin_lock_irqsave(&worker->lock, flags);
	work->canceling--;

out_fast:
	spin_unlock_irqrestore(&worker->lock, flags);
out:
	return ret;
}

/**
 * kthread_cancel_work_sync - cancel a kthread work and wait for it to finish
 * @work: the kthread work to cancel
 *
 * Cancel @work and wait for its execution to finish.  This function
 * can be used even if the work re-queues itself. On return from this
 * function, @work is guaranteed to be not pending or executing on any CPU.
 *
 * kthread_cancel_work_sync(&delayed_work->work) must not be used for
 * delayed_work's. Use kthread_cancel_delayed_work_sync() instead.
 *
 * The caller must ensure that the worker on which @work was last
 * queued can't be destroyed before this function returns.
 *
 * Return: %true if @work was pending, %false otherwise.
 */
bool kthread_cancel_work_sync(struct kthread_work *work)
{
	return __kthread_cancel_work_sync(work, false);
}
EXPORT_SYMBOL_GPL(kthread_cancel_work_sync);

/**
 * kthread_cancel_delayed_work_sync - cancel a kthread delayed work and
 *	wait for it to finish.
 * @dwork: the kthread delayed work to cancel
 *
 * This is kthread_cancel_work_sync() for delayed works.
 *
 * Return: %true if @dwork was pending, %false otherwise.
 */
bool kthread_cancel_delayed_work_sync(struct kthread_delayed_work *dwork)
{
	return __kthread_cancel_work_sync(&dwork->work, true);
}
EXPORT_SYMBOL_GPL(kthread_cancel_delayed_work_sync);

/**
 * kthread_flush_worker - flush all current works on a kthread_worker
 * @worker: worker to flush
 *
 * Wait until all currently executing or pending works on @worker are
 * finished.
 */
void kthread_flush_worker(struct kthread_worker *worker)
{
	struct kthread_flush_work fwork = {
		KTHREAD_WORK_INIT(fwork.work, kthread_flush_work_fn),
		COMPLETION_INITIALIZER_ONSTACK(fwork.done),
	};

	kthread_queue_work(worker, &fwork.work);
	wait_for_completion(&fwork.done);
}
EXPORT_SYMBOL_GPL(kthread_flush_worker);

/**
 * kthread_destroy_worker - destroy a kthread worker
 * @worker: worker to be destroyed
 *
 * Flush and destroy @worker.  The simple flush is enough because the kthread
 * worker API is used only in trivial scenarios.  There are no multi-step state
 * machines needed.
 */
void kthread_destroy_worker(struct kthread_worker *worker)
{
	struct task_struct *task;

	task = worker->task;
	if (WARN_ON(!task))
		return;

	kthread_flush_worker(worker);
	kthread_stop(task);
	WARN_ON(!list_empty(&worker->work_list));
	kfree(worker);
}
EXPORT_SYMBOL(kthread_destroy_worker);

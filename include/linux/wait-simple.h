#ifndef _LINUX_WAIT_SIMPLE_H
#define _LINUX_WAIT_SIMPLE_H

#include <linux/spinlock.h>
#include <linux/list.h>

#include <asm/current.h>

struct swaiter {
	struct task_struct	*task;
	struct list_head	node;
};

#define DEFINE_SWAITER(name)					\
	struct swaiter name = {					\
		.task	= current,				\
		.node	= LIST_HEAD_INIT((name).node),		\
	}

struct swait_head {
	raw_spinlock_t		lock;
	struct list_head	list;
};

#define SWAIT_HEAD_INITIALIZER(name) {				\
		.lock	= __RAW_SPIN_LOCK_UNLOCKED(name.lock),	\
		.list	= LIST_HEAD_INIT((name).list),		\
	}

#define DEFINE_SWAIT_HEAD(name)					\
	struct swait_head name = SWAIT_HEAD_INITIALIZER(name)

extern void __init_swait_head(struct swait_head *h, struct lock_class_key *key);

#define init_swait_head(swh)					\
	do {							\
		static struct lock_class_key __key;		\
								\
		__init_swait_head((swh), &__key);		\
	} while (0)

/*
 * Waiter functions
 */
extern void swait_prepare_locked(struct swait_head *head, struct swaiter *w);
extern void swait_prepare(struct swait_head *head, struct swaiter *w, int state);
extern void swait_finish_locked(struct swait_head *head, struct swaiter *w);
extern void swait_finish(struct swait_head *head, struct swaiter *w);

/* Check whether a head has waiters enqueued */
static inline bool swaitqueue_active(struct swait_head *h)
{
	/* Make sure the condition is visible before checking list_empty() */
	smp_mb();
	return !list_empty(&h->list);
}

/*
 * Wakeup functions
 */
extern unsigned int __swait_wake(struct swait_head *head, unsigned int state, unsigned int num);
extern unsigned int __swait_wake_locked(struct swait_head *head, unsigned int state, unsigned int num);

#define swait_wake(head)			__swait_wake(head, TASK_NORMAL, 1)
#define swait_wake_interruptible(head)		__swait_wake(head, TASK_INTERRUPTIBLE, 1)
#define swait_wake_all(head)			__swait_wake(head, TASK_NORMAL, 0)
#define swait_wake_all_interruptible(head)	__swait_wake(head, TASK_INTERRUPTIBLE, 0)

/*
 * Event API
 */
#define __swait_event(wq, condition)					\
do {									\
	DEFINE_SWAITER(__wait);						\
									\
	for (;;) {							\
		swait_prepare(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		schedule();						\
	}								\
	swait_finish(&wq, &__wait);					\
} while (0)

/**
 * swait_event - sleep until a condition gets true
 * @wq: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 *
 * The process is put to sleep (TASK_UNINTERRUPTIBLE) until the
 * @condition evaluates to true. The @condition is checked each time
 * the waitqueue @wq is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 */
#define swait_event(wq, condition)					\
do {									\
	if (condition)							\
		break;							\
	__swait_event(wq, condition);					\
} while (0)

#define __swait_event_interruptible(wq, condition, ret)			\
do {									\
	DEFINE_SWAITER(__wait);						\
									\
	for (;;) {							\
		swait_prepare(&wq, &__wait, TASK_INTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		if (signal_pending(current)) {				\
			ret = -ERESTARTSYS;				\
			break;						\
		}							\
		schedule();						\
	}								\
	swait_finish(&wq, &__wait);					\
} while (0)

#define __swait_event_interruptible_timeout(wq, condition, ret)		\
do {									\
	DEFINE_SWAITER(__wait);						\
									\
	for (;;) {							\
		swait_prepare(&wq, &__wait, TASK_INTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		if (signal_pending(current)) {				\
			ret = -ERESTARTSYS;				\
			break;						\
		}							\
		ret = schedule_timeout(ret);				\
		if (!ret)						\
			break;						\
	}								\
	swait_finish(&wq, &__wait);					\
} while (0)

/**
 * swait_event_interruptible - sleep until a condition gets true
 * @wq: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 *
 * The process is put to sleep (TASK_INTERRUPTIBLE) until the
 * @condition evaluates to true. The @condition is checked each time
 * the waitqueue @wq is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 */
#define swait_event_interruptible(wq, condition)			\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__swait_event_interruptible(wq, condition, __ret);	\
	__ret;								\
})

#define swait_event_interruptible_timeout(wq, condition, timeout)	\
({									\
	int __ret = timeout;						\
	if (!(condition))						\
		__swait_event_interruptible_timeout(wq, condition, __ret);	\
	__ret;								\
})

#define __swait_event_timeout(wq, condition, ret)			\
do {									\
	DEFINE_SWAITER(__wait);						\
									\
	for (;;) {							\
		swait_prepare(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		ret = schedule_timeout(ret);				\
		if (!ret)						\
			break;						\
	}								\
	swait_finish(&wq, &__wait);					\
} while (0)

/**
 * swait_event_timeout - sleep until a condition gets true or a timeout elapses
 * @wq: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 * @timeout: timeout, in jiffies
 *
 * The process is put to sleep (TASK_UNINTERRUPTIBLE) until the
 * @condition evaluates to true. The @condition is checked each time
 * the waitqueue @wq is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 *
 * The function returns 0 if the @timeout elapsed, and the remaining
 * jiffies if the condition evaluated to true before the timeout elapsed.
 */
#define swait_event_timeout(wq, condition, timeout)			\
({									\
	long __ret = timeout;						\
	if (!(condition))						\
		__swait_event_timeout(wq, condition, __ret);		\
	__ret;								\
})

#endif

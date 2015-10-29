/*
 * Simple waitqueues without fancy flags and callbacks
 *
 * (C) 2011 Thomas Gleixner <tglx@linutronix.de>
 *
 * Based on kernel/wait.c
 *
 * For licencing details see kernel-base/COPYING
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/wait-simple.h>

/* Adds w to head->list. Must be called with head->lock locked. */
static inline void __swait_enqueue(struct swait_head *head, struct swaiter *w)
{
	list_add(&w->node, &head->list);
	/* We can't let the condition leak before the setting of head */
	smp_mb();
}

/* Removes w from head->list. Must be called with head->lock locked. */
static inline void __swait_dequeue(struct swaiter *w)
{
	list_del_init(&w->node);
}

void __init_swait_head(struct swait_head *head, struct lock_class_key *key)
{
	raw_spin_lock_init(&head->lock);
	lockdep_set_class(&head->lock, key);
	INIT_LIST_HEAD(&head->list);
}
EXPORT_SYMBOL(__init_swait_head);

void swait_prepare_locked(struct swait_head *head, struct swaiter *w)
{
	w->task = current;
	if (list_empty(&w->node))
		__swait_enqueue(head, w);
}

void swait_prepare(struct swait_head *head, struct swaiter *w, int state)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&head->lock, flags);
	swait_prepare_locked(head, w);
	__set_current_state(state);
	raw_spin_unlock_irqrestore(&head->lock, flags);
}
EXPORT_SYMBOL(swait_prepare);

void swait_finish_locked(struct swait_head *head, struct swaiter *w)
{
	__set_current_state(TASK_RUNNING);
	if (w->task)
		__swait_dequeue(w);
}

void swait_finish(struct swait_head *head, struct swaiter *w)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	if (w->task) {
		raw_spin_lock_irqsave(&head->lock, flags);
		__swait_dequeue(w);
		raw_spin_unlock_irqrestore(&head->lock, flags);
	}
}
EXPORT_SYMBOL(swait_finish);

unsigned int
__swait_wake_locked(struct swait_head *head, unsigned int state, unsigned int num)
{
	struct swaiter *curr, *next;
	int woken = 0;

	list_for_each_entry_safe(curr, next, &head->list, node) {
		if (wake_up_state(curr->task, state)) {
			__swait_dequeue(curr);
			/*
			 * The waiting task can free the waiter as
			 * soon as curr->task = NULL is written,
			 * without taking any locks. A memory barrier
			 * is required here to prevent the following
			 * store to curr->task from getting ahead of
			 * the dequeue operation.
			 */
			smp_wmb();
			curr->task = NULL;
			if (++woken == num)
				break;
		}
	}
	return woken;
}

unsigned int
__swait_wake(struct swait_head *head, unsigned int state, unsigned int num)
{
	unsigned long flags;
	int woken;

	if (!swaitqueue_active(head))
		return 0;

	raw_spin_lock_irqsave(&head->lock, flags);
	woken = __swait_wake_locked(head, state, num);
	raw_spin_unlock_irqrestore(&head->lock, flags);
	return woken;
}
EXPORT_SYMBOL(__swait_wake);

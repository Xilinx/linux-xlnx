#ifndef _LINUX_JBD_STATE_H
#define _LINUX_JBD_STATE_H

#include <linux/bit_spinlock.h>

static inline struct buffer_head *jh2bh(struct journal_head *jh)
{
	return jh->b_bh;
}

static inline struct journal_head *bh2jh(struct buffer_head *bh)
{
	return bh->b_private;
}

static inline void jbd_lock_bh_state(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	bit_spin_lock(BH_State, &bh->b_state);
#else
	spin_lock(&bh->b_state_lock);
#endif
}

static inline int jbd_trylock_bh_state(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	return bit_spin_trylock(BH_State, &bh->b_state);
#else
	return spin_trylock(&bh->b_state_lock);
#endif
}

static inline int jbd_is_locked_bh_state(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	return bit_spin_is_locked(BH_State, &bh->b_state);
#else
	return spin_is_locked(&bh->b_state_lock);
#endif
}

static inline void jbd_unlock_bh_state(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	bit_spin_unlock(BH_State, &bh->b_state);
#else
	spin_unlock(&bh->b_state_lock);
#endif
}

static inline void jbd_lock_bh_journal_head(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	bit_spin_lock(BH_JournalHead, &bh->b_state);
#else
	spin_lock(&bh->b_journal_head_lock);
#endif
}

static inline void jbd_unlock_bh_journal_head(struct buffer_head *bh)
{
#ifndef CONFIG_PREEMPT_RT_BASE
	bit_spin_unlock(BH_JournalHead, &bh->b_state);
#else
	spin_unlock(&bh->b_journal_head_lock);
#endif
}

#endif

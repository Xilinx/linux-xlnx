/*
 * jump label support
 *
 * Copyright (C) 2009 Jason Baron <jbaron@redhat.com>
 * Copyright (C) 2011 Peter Zijlstra
 *
 */
#include <linux/memory.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/err.h>
#include <linux/static_key.h>
#include <linux/jump_label_ratelimit.h>
#include <linux/bug.h>

#ifdef HAVE_JUMP_LABEL

/* mutex to protect coming/going of the the jump_label table */
static DEFINE_MUTEX(jump_label_mutex);

void jump_label_lock(void)
{
	mutex_lock(&jump_label_mutex);
}

void jump_label_unlock(void)
{
	mutex_unlock(&jump_label_mutex);
}

static int jump_label_cmp(const void *a, const void *b)
{
	const struct jump_entry *jea = a;
	const struct jump_entry *jeb = b;

	if (jea->key < jeb->key)
		return -1;

	if (jea->key > jeb->key)
		return 1;

	return 0;
}

static void
jump_label_sort_entries(struct jump_entry *start, struct jump_entry *stop)
{
	unsigned long size;

	size = (((unsigned long)stop - (unsigned long)start)
					/ sizeof(struct jump_entry));
	sort(start, size, sizeof(struct jump_entry), jump_label_cmp, NULL);
}

static void jump_label_update(struct static_key *key);

/*
 * There are similar definitions for the !HAVE_JUMP_LABEL case in jump_label.h.
 * The use of 'atomic_read()' requires atomic.h and its problematic for some
 * kernel headers such as kernel.h and others. Since static_key_count() is not
 * used in the branch statements as it is for the !HAVE_JUMP_LABEL case its ok
 * to have it be a function here. Similarly, for 'static_key_enable()' and
 * 'static_key_disable()', which require bug.h. This should allow jump_label.h
 * to be included from most/all places for HAVE_JUMP_LABEL.
 */
int static_key_count(struct static_key *key)
{
	/*
	 * -1 means the first static_key_slow_inc() is in progress.
	 *  static_key_enabled() must return true, so return 1 here.
	 */
	int n = atomic_read(&key->enabled);

	return n >= 0 ? n : 1;
}
EXPORT_SYMBOL_GPL(static_key_count);

void static_key_enable(struct static_key *key)
{
	int count = static_key_count(key);

	WARN_ON_ONCE(count < 0 || count > 1);

	if (!count)
		static_key_slow_inc(key);
}
EXPORT_SYMBOL_GPL(static_key_enable);

void static_key_disable(struct static_key *key)
{
	int count = static_key_count(key);

	WARN_ON_ONCE(count < 0 || count > 1);

	if (count)
		static_key_slow_dec(key);
}
EXPORT_SYMBOL_GPL(static_key_disable);

void static_key_slow_inc(struct static_key *key)
{
	int v, v1;

	STATIC_KEY_CHECK_USE();

	/*
	 * Careful if we get concurrent static_key_slow_inc() calls;
	 * later calls must wait for the first one to _finish_ the
	 * jump_label_update() process.  At the same time, however,
	 * the jump_label_update() call below wants to see
	 * static_key_enabled(&key) for jumps to be updated properly.
	 *
	 * So give a special meaning to negative key->enabled: it sends
	 * static_key_slow_inc() down the slow path, and it is non-zero
	 * so it counts as "enabled" in jump_label_update().  Note that
	 * atomic_inc_unless_negative() checks >= 0, so roll our own.
	 */
	for (v = atomic_read(&key->enabled); v > 0; v = v1) {
		v1 = atomic_cmpxchg(&key->enabled, v, v + 1);
		if (likely(v1 == v))
			return;
	}

	jump_label_lock();
	if (atomic_read(&key->enabled) == 0) {
		atomic_set(&key->enabled, -1);
		jump_label_update(key);
		atomic_set(&key->enabled, 1);
	} else {
		atomic_inc(&key->enabled);
	}
	jump_label_unlock();
}
EXPORT_SYMBOL_GPL(static_key_slow_inc);

static void __static_key_slow_dec(struct static_key *key,
		unsigned long rate_limit, struct delayed_work *work)
{
	/*
	 * The negative count check is valid even when a negative
	 * key->enabled is in use by static_key_slow_inc(); a
	 * __static_key_slow_dec() before the first static_key_slow_inc()
	 * returns is unbalanced, because all other static_key_slow_inc()
	 * instances block while the update is in progress.
	 */
	if (!atomic_dec_and_mutex_lock(&key->enabled, &jump_label_mutex)) {
		WARN(atomic_read(&key->enabled) < 0,
		     "jump label: negative count!\n");
		return;
	}

	if (rate_limit) {
		atomic_inc(&key->enabled);
		schedule_delayed_work(work, rate_limit);
	} else {
		jump_label_update(key);
	}
	jump_label_unlock();
}

static void jump_label_update_timeout(struct work_struct *work)
{
	struct static_key_deferred *key =
		container_of(work, struct static_key_deferred, work.work);
	__static_key_slow_dec(&key->key, 0, NULL);
}

void static_key_slow_dec(struct static_key *key)
{
	STATIC_KEY_CHECK_USE();
	__static_key_slow_dec(key, 0, NULL);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec);

void static_key_slow_dec_deferred(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE();
	__static_key_slow_dec(&key->key, key->timeout, &key->work);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec_deferred);

void jump_label_rate_limit(struct static_key_deferred *key,
		unsigned long rl)
{
	STATIC_KEY_CHECK_USE();
	key->timeout = rl;
	INIT_DELAYED_WORK(&key->work, jump_label_update_timeout);
}
EXPORT_SYMBOL_GPL(jump_label_rate_limit);

static int addr_conflict(struct jump_entry *entry, void *start, void *end)
{
	if (entry->code <= (unsigned long)end &&
		entry->code + JUMP_LABEL_NOP_SIZE > (unsigned long)start)
		return 1;

	return 0;
}

static int __jump_label_text_reserved(struct jump_entry *iter_start,
		struct jump_entry *iter_stop, void *start, void *end)
{
	struct jump_entry *iter;

	iter = iter_start;
	while (iter < iter_stop) {
		if (addr_conflict(iter, start, end))
			return 1;
		iter++;
	}

	return 0;
}

/*
 * Update code which is definitely not currently executing.
 * Architectures which need heavyweight synchronization to modify
 * running code can override this to make the non-live update case
 * cheaper.
 */
void __weak __init_or_module arch_jump_label_transform_static(struct jump_entry *entry,
					    enum jump_label_type type)
{
	arch_jump_label_transform(entry, type);
}

static inline struct jump_entry *static_key_entries(struct static_key *key)
{
	return (struct jump_entry *)((unsigned long)key->entries & ~JUMP_TYPE_MASK);
}

static inline bool static_key_type(struct static_key *key)
{
	return (unsigned long)key->entries & JUMP_TYPE_MASK;
}

static inline struct static_key *jump_entry_key(struct jump_entry *entry)
{
	return (struct static_key *)((unsigned long)entry->key & ~1UL);
}

static bool jump_entry_branch(struct jump_entry *entry)
{
	return (unsigned long)entry->key & 1UL;
}

static enum jump_label_type jump_label_type(struct jump_entry *entry)
{
	struct static_key *key = jump_entry_key(entry);
	bool enabled = static_key_enabled(key);
	bool branch = jump_entry_branch(entry);

	/* See the comment in linux/jump_label.h */
	return enabled ^ branch;
}

static void __jump_label_update(struct static_key *key,
				struct jump_entry *entry,
				struct jump_entry *stop)
{
	for (; (entry < stop) && (jump_entry_key(entry) == key); entry++) {
		/*
		 * entry->code set to 0 invalidates module init text sections
		 * kernel_text_address() verifies we are not in core kernel
		 * init code, see jump_label_invalidate_module_init().
		 */
		if (entry->code && kernel_text_address(entry->code))
			arch_jump_label_transform(entry, jump_label_type(entry));
	}
}

void __init jump_label_init(void)
{
	struct jump_entry *iter_start = __start___jump_table;
	struct jump_entry *iter_stop = __stop___jump_table;
	struct static_key *key = NULL;
	struct jump_entry *iter;

	/*
	 * Since we are initializing the static_key.enabled field with
	 * with the 'raw' int values (to avoid pulling in atomic.h) in
	 * jump_label.h, let's make sure that is safe. There are only two
	 * cases to check since we initialize to 0 or 1.
	 */
	BUILD_BUG_ON((int)ATOMIC_INIT(0) != 0);
	BUILD_BUG_ON((int)ATOMIC_INIT(1) != 1);

	if (static_key_initialized)
		return;

	jump_label_lock();
	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		/* rewrite NOPs */
		if (jump_label_type(iter) == JUMP_LABEL_NOP)
			arch_jump_label_transform_static(iter, JUMP_LABEL_NOP);

		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		/*
		 * Set key->entries to iter, but preserve JUMP_LABEL_TRUE_BRANCH.
		 */
		*((unsigned long *)&key->entries) += (unsigned long)iter;
#ifdef CONFIG_MODULES
		key->next = NULL;
#endif
	}
	static_key_initialized = true;
	jump_label_unlock();
}

#ifdef CONFIG_MODULES

static enum jump_label_type jump_label_init_type(struct jump_entry *entry)
{
	struct static_key *key = jump_entry_key(entry);
	bool type = static_key_type(key);
	bool branch = jump_entry_branch(entry);

	/* See the comment in linux/jump_label.h */
	return type ^ branch;
}

struct static_key_mod {
	struct static_key_mod *next;
	struct jump_entry *entries;
	struct module *mod;
};

static int __jump_label_mod_text_reserved(void *start, void *end)
{
	struct module *mod;

	preempt_disable();
	mod = __module_text_address((unsigned long)start);
	WARN_ON_ONCE(__module_text_address((unsigned long)end) != mod);
	preempt_enable();

	if (!mod)
		return 0;


	return __jump_label_text_reserved(mod->jump_entries,
				mod->jump_entries + mod->num_jump_entries,
				start, end);
}

static void __jump_label_mod_update(struct static_key *key)
{
	struct static_key_mod *mod;

	for (mod = key->next; mod; mod = mod->next) {
		struct module *m = mod->mod;

		__jump_label_update(key, mod->entries,
				    m->jump_entries + m->num_jump_entries);
	}
}

/***
 * apply_jump_label_nops - patch module jump labels with arch_get_jump_label_nop()
 * @mod: module to patch
 *
 * Allow for run-time selection of the optimal nops. Before the module
 * loads patch these with arch_get_jump_label_nop(), which is specified by
 * the arch specific jump label code.
 */
void jump_label_apply_nops(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return;

	for (iter = iter_start; iter < iter_stop; iter++) {
		/* Only write NOPs for arch_branch_static(). */
		if (jump_label_init_type(iter) == JUMP_LABEL_NOP)
			arch_jump_label_transform_static(iter, JUMP_LABEL_NOP);
	}
}

static int jump_label_add_module(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;
	struct static_key *key = NULL;
	struct static_key_mod *jlm;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return 0;

	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		if (within_module(iter->key, mod)) {
			/*
			 * Set key->entries to iter, but preserve JUMP_LABEL_TRUE_BRANCH.
			 */
			*((unsigned long *)&key->entries) += (unsigned long)iter;
			key->next = NULL;
			continue;
		}
		jlm = kzalloc(sizeof(struct static_key_mod), GFP_KERNEL);
		if (!jlm)
			return -ENOMEM;
		jlm->mod = mod;
		jlm->entries = iter;
		jlm->next = key->next;
		key->next = jlm;

		/* Only update if we've changed from our initial state */
		if (jump_label_type(iter) != jump_label_init_type(iter))
			__jump_label_update(key, iter, iter_stop);
	}

	return 0;
}

static void jump_label_del_module(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;
	struct static_key *key = NULL;
	struct static_key_mod *jlm, **prev;

	for (iter = iter_start; iter < iter_stop; iter++) {
		if (jump_entry_key(iter) == key)
			continue;

		key = jump_entry_key(iter);

		if (within_module(iter->key, mod))
			continue;

		prev = &key->next;
		jlm = key->next;

		while (jlm && jlm->mod != mod) {
			prev = &jlm->next;
			jlm = jlm->next;
		}

		if (jlm) {
			*prev = jlm->next;
			kfree(jlm);
		}
	}
}

static void jump_label_invalidate_module_init(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;

	for (iter = iter_start; iter < iter_stop; iter++) {
		if (within_module_init(iter->code, mod))
			iter->code = 0;
	}
}

static int
jump_label_module_notify(struct notifier_block *self, unsigned long val,
			 void *data)
{
	struct module *mod = data;
	int ret = 0;

	switch (val) {
	case MODULE_STATE_COMING:
		jump_label_lock();
		ret = jump_label_add_module(mod);
		if (ret)
			jump_label_del_module(mod);
		jump_label_unlock();
		break;
	case MODULE_STATE_GOING:
		jump_label_lock();
		jump_label_del_module(mod);
		jump_label_unlock();
		break;
	case MODULE_STATE_LIVE:
		jump_label_lock();
		jump_label_invalidate_module_init(mod);
		jump_label_unlock();
		break;
	}

	return notifier_from_errno(ret);
}

static struct notifier_block jump_label_module_nb = {
	.notifier_call = jump_label_module_notify,
	.priority = 1, /* higher than tracepoints */
};

static __init int jump_label_init_module(void)
{
	return register_module_notifier(&jump_label_module_nb);
}
early_initcall(jump_label_init_module);

#endif /* CONFIG_MODULES */

/***
 * jump_label_text_reserved - check if addr range is reserved
 * @start: start text addr
 * @end: end text addr
 *
 * checks if the text addr located between @start and @end
 * overlaps with any of the jump label patch addresses. Code
 * that wants to modify kernel text should first verify that
 * it does not overlap with any of the jump label addresses.
 * Caller must hold jump_label_mutex.
 *
 * returns 1 if there is an overlap, 0 otherwise
 */
int jump_label_text_reserved(void *start, void *end)
{
	int ret = __jump_label_text_reserved(__start___jump_table,
			__stop___jump_table, start, end);

	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	ret = __jump_label_mod_text_reserved(start, end);
#endif
	return ret;
}

static void jump_label_update(struct static_key *key)
{
	struct jump_entry *stop = __stop___jump_table;
	struct jump_entry *entry = static_key_entries(key);
#ifdef CONFIG_MODULES
	struct module *mod;

	__jump_label_mod_update(key);

	preempt_disable();
	mod = __module_address((unsigned long)key);
	if (mod)
		stop = mod->jump_entries + mod->num_jump_entries;
	preempt_enable();
#endif
	/* if there are no users, entry can be NULL */
	if (entry)
		__jump_label_update(key, entry, stop);
}

#ifdef CONFIG_STATIC_KEYS_SELFTEST
static DEFINE_STATIC_KEY_TRUE(sk_true);
static DEFINE_STATIC_KEY_FALSE(sk_false);

static __init int jump_label_test(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		WARN_ON(static_key_enabled(&sk_true.key) != true);
		WARN_ON(static_key_enabled(&sk_false.key) != false);

		WARN_ON(!static_branch_likely(&sk_true));
		WARN_ON(!static_branch_unlikely(&sk_true));
		WARN_ON(static_branch_likely(&sk_false));
		WARN_ON(static_branch_unlikely(&sk_false));

		static_branch_disable(&sk_true);
		static_branch_enable(&sk_false);

		WARN_ON(static_key_enabled(&sk_true.key) == true);
		WARN_ON(static_key_enabled(&sk_false.key) == false);

		WARN_ON(static_branch_likely(&sk_true));
		WARN_ON(static_branch_unlikely(&sk_true));
		WARN_ON(!static_branch_likely(&sk_false));
		WARN_ON(!static_branch_unlikely(&sk_false));

		static_branch_enable(&sk_true);
		static_branch_disable(&sk_false);
	}

	return 0;
}
late_initcall(jump_label_test);
#endif /* STATIC_KEYS_SELFTEST */

#endif /* HAVE_JUMP_LABEL */

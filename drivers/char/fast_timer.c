/*
 * linux/drivers/char/fast_timer.c
 *
 * Fast timer code for general use, primarily polling network chips
 *
 * Copyright (c) 2004 SnapGear Inc. <www.snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/fast_timer.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/fast_timer.h>


struct ftentry {
	void	(*func)(void *);
	void	*arg;
};

#define	FAST_TIMER_MAX	8
static struct ftentry	fast_timer[FAST_TIMER_MAX];
static int		fast_timers = 0;
static spinlock_t	fast_timer_lock;
static int		fast_timer_rate;

void fast_timer_add(void (*func)(void *arg), void *arg)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&fast_timer_lock, flags);
	for (i = 0; i < fast_timers; i++) {
		if (fast_timer[i].func == func && fast_timer[i].arg == arg) {
			spin_unlock_irqrestore(&fast_timer_lock, flags);
			printk(KERN_ERR
					"fast_timer: entry already exists (0x%x, 0x%x)\n",
					(unsigned int) func, (unsigned int) arg);
			return;
		}
	}

	if (fast_timers >= FAST_TIMER_MAX) {
		spin_unlock_irqrestore(&fast_timer_lock, flags);
		printk(KERN_ERR "fast timer: no free slots\n");
		return;
	}

	fast_timer[fast_timers].func = func;
	fast_timer[fast_timers].arg = arg;
	fast_timers++;
	spin_unlock_irqrestore(&fast_timer_lock, flags);
}

void fast_timer_remove(void (*func)(void *arg), void *arg)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&fast_timer_lock, flags);
	for (i = 0; i < fast_timers; i++) {
		if (fast_timer[i].func == func && fast_timer[i].arg == arg) {
			memmove(&fast_timer[i], &fast_timer[i+1],
					sizeof(struct ftentry) * (FAST_TIMER_MAX - (i+1)));
			fast_timers--;
			spin_unlock_irqrestore(&fast_timer_lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&fast_timer_lock, flags);
	printk(KERN_ERR "fast timer: entry does not exist (0x%x, 0x%x)\n",
			(unsigned int) func, (unsigned int) arg);
}

static void do_fast_timer(void)
{
	int i;

	for (i = 0; i < fast_timers; i++)
		(*fast_timer[i].func)(fast_timer[i].arg);
}

#include <asm/fast_timer.h>

#ifdef CONFIG_SYSCTL
int fast_timer_sysctl(ctl_table *ctl, int write,
        struct file * filp, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
	if (write && (*valp != val))
		fast_timer_set();
	return ret;
}

static ctl_table dev_table[] = {
	{2, "fast_timer",
	 &fast_timer_rate, sizeof(int), 0644, NULL, &fast_timer_sysctl},
	{0}
};

static ctl_table root_table[] = {
	{CTL_DEV, "dev", NULL, 0, 0555, dev_table},
	{0}
};

static struct ctl_table_header *sysctl_header;

static void __init init_sysctl(void)
{
	sysctl_header = register_sysctl_table(root_table, 0);
}

static void __exit cleanup_sysctl(void)
{
	unregister_sysctl_table(sysctl_header);
}

#else

static inline void init_sysctl(void)
{
}

static inline void cleanup_sysctl(void)
{
}

#endif

static int __init fast_timer_init(void)
{
	int ret;

	spin_lock_init(&fast_timer_lock);

	ret = fast_timer_setup();
	if (ret != 0)
		return ret;

	init_sysctl();
	return 0;
}

static void __exit fast_timer_exit(void)
{
	cleanup_sysctl();
	fast_timer_cleanup();
}

module_init(fast_timer_init);
module_exit(fast_timer_exit);
EXPORT_SYMBOL(fast_timer_add);
EXPORT_SYMBOL(fast_timer_remove);
MODULE_AUTHOR("Philip Craig <philipc@snapgear.com>");
MODULE_DESCRIPTION("Driver for general purpose fast timer");
MODULE_LICENSE("GPL");

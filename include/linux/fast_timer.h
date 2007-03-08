#ifndef _LINUX_FAST_TIMER_H
#define _LINUX_FAST_TIMER_H

extern void fast_timer_add(void (*func)(void *arg), void *arg);
extern void fast_timer_remove(void (*func)(void *arg), void *arg);
extern unsigned long fast_timer_count(void);

#endif

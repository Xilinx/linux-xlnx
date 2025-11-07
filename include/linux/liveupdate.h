/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#ifndef _LINUX_LIVEUPDATE_H
#define _LINUX_LIVEUPDATE_H

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/list.h>

#ifdef CONFIG_LIVEUPDATE

void __init liveupdate_init(void);

/* Return true if live update orchestrator is enabled */
bool liveupdate_enabled(void);

/* Called during kexec to tell LUO that entered into reboot */
int liveupdate_reboot(void);

#else /* CONFIG_LIVEUPDATE */

static inline void liveupdate_init(void)
{
}

static inline bool liveupdate_enabled(void)
{
	return false;
}

static inline int liveupdate_reboot(void)
{
	return 0;
}

#endif /* CONFIG_LIVEUPDATE */
#endif /* _LINUX_LIVEUPDATE_H */

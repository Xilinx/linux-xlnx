/*
 * r8a7779 Power management support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/soc/renesas/rcar-sysc.h>

#include <asm/io.h>

#include "r8a7779.h"

/* SYSC */
#define SYSCIER 0x0c
#define SYSCIMR 0x10

#if defined(CONFIG_PM) || defined(CONFIG_SMP)

static void __init r8a7779_sysc_init(void)
{
	rcar_sysc_init(0xffd85000, 0x0131000e);
}

#else /* CONFIG_PM || CONFIG_SMP */

static inline void r8a7779_sysc_init(void) {}

#endif /* CONFIG_PM || CONFIG_SMP */

void __init r8a7779_pm_init(void)
{
	static int once;

	if (!once++)
		r8a7779_sysc_init();
}

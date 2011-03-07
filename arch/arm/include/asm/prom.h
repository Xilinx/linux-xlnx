/*
 *  arch/arm/include/asm/prom.h
 *
 *  Copyright (C) 2009 Canonical Ltd. <jeremy.kerr@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __ASMARM_PROM_H
#define __ASMARM_PROM_H

#ifdef CONFIG_OF

#include <asm/setup.h>
#include <asm/irq.h>

static inline void irq_dispose_mapping(unsigned int virq)
{
	return;
}

extern void arm_unflatten_device_tree(void);
extern struct machine_desc *setup_machine_fdt(unsigned int dt_phys);

#else /* CONFIG_OF */

static void arm_unflatten_device_tree(void) { }
static inline struct machine_desc *setup_machine_fdt(unsigned int dt_phys)
{
	return NULL;
}

#endif /* CONFIG_OF */
#endif /* ASMARM_PROM_H */

/*
 *  linux/arch/arm/kernel/devtree.c
 *
 *  Copyright (C) 2009 Canonical Ltd. <jeremy.kerr@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/mach/arch.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	arm_add_memory(base, size);
}

void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return alloc_bootmem_align(size, align);
}

/**
 * irq_create_of_mapping - Hook to resolve OF irq specifier into a Linux irq#
 *
 * Currently the mapping mechanism is trivial; simple flat hwirq numbers are
 * mapped 1:1 onto Linux irq numbers.  Cascaded irq controllers are not
 * supported.
 */
unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	return intspec[0];
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

extern struct machine_desc __arch_info_begin, __arch_info_end;

/**
 * arm_unflatten_device_tree - Copy dtb into a safe area and unflatten it.
 *
 * Copies the dtb out of initial memory and into an allocated block so that
 * it doesn't get overwritten by the kernel and then unflatten it into the
 * live tree representation.
 */
void __init arm_unflatten_device_tree(void)
{
	struct boot_param_header *devtree;
	u32 dtb_size;

	if (!initial_boot_params)
		return;

	/* Save the dtb to an allocated buffer */
	dtb_size = be32_to_cpu(initial_boot_params->totalsize);
	devtree = early_init_dt_alloc_memory_arch(dtb_size, SZ_4K);
	if (!devtree) {
		printk("Unable to allocate memory for device tree\n");
		while(1);
	}
	pr_info("relocating device tree from 0x%p to 0x%p, length 0x%x\n",
		initial_boot_params, devtree, dtb_size);
	memmove(devtree, initial_boot_params, dtb_size);
	initial_boot_params = devtree;

	unflatten_device_tree();
}

/**
 * setup_machine_fdt - Machine setup when an dtb was passed to the kernel
 * @dt_phys: physical address of dt blob
 *
 * If a dtb was passed to the kernel in r2, then use it to choose the
 * correct machine_desc and to setup the system.
 */
struct machine_desc * __init setup_machine_fdt(unsigned int dt_phys)
{
	struct boot_param_header *devtree = phys_to_virt(dt_phys);
	struct machine_desc *mdesc, *mdesc_best = NULL;
	unsigned int score, mdesc_score = ~1;
	unsigned long dt_root;
	const char *model;

	/* check device tree validity */
	if (!dt_phys || be32_to_cpu(devtree->magic) != OF_DT_HEADER)
		return NULL;

	/* Search the mdescs for the 'best' compatible value match */
	initial_boot_params = devtree;

	dt_root = of_get_flat_dt_root();
	for (mdesc = &__arch_info_begin; mdesc < &__arch_info_end; mdesc++) {
		score = of_flat_dt_match(dt_root, mdesc->dt_compat);
		if (score > 0 && score < mdesc_score) {
			mdesc_best = mdesc;
			mdesc_score = score;
		}
	}
	if (!mdesc_best) {
		printk("Machine not supported, unable to continue.\n");
		while (1);
	}

	model = of_get_flat_dt_prop(dt_root, "model", NULL);
	if (!model)
		model = of_get_flat_dt_prop(dt_root, "compatible", NULL);
	if (!model)
		model = "<unknown>";
	pr_info("Machine: %s, model: %s\n", mdesc_best->name, model);

	/* Retrieve various information from the /chosen node */
	of_scan_flat_dt(early_init_dt_scan_chosen, NULL);
	/* Initialize {size,address}-cells info */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);
	/* Save command line for /proc/cmdline  */
	strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);

	return mdesc_best;
}

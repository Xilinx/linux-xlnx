/*
 * Copyright (c) 2016 Christoph Hellwig.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/kobject.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-pci.h>
#include <linux/pci.h>
#include <linux/module.h>

/**
 * blk_mq_pci_map_queues - provide a default queue mapping for PCI device
 * @set:	tagset to provide the mapping for
 * @pdev:	PCI device associated with @set.
 *
 * This function assumes the PCI device @pdev has at least as many available
 * interrupt vetors as @set has queues.  It will then queuery the vector
 * corresponding to each queue for it's affinity mask and built queue mapping
 * that maps a queue to the CPUs that have irq affinity for the corresponding
 * vector.
 */
int blk_mq_pci_map_queues(struct blk_mq_tag_set *set, struct pci_dev *pdev)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	for (queue = 0; queue < set->nr_hw_queues; queue++) {
		mask = pci_irq_get_affinity(pdev, queue);
		if (!mask)
			return -EINVAL;

		for_each_cpu(cpu, mask)
			set->mq_map[cpu] = queue;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(blk_mq_pci_map_queues);

/*
 * Generic show_mem() implementation
 *
 * Copyright (C) 2008 Johannes Weiner <hannes@saeurebad.de>
 * All code subject to the GPL version 2.
 */

#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/quicklist.h>

void show_mem(unsigned int filter)
{
	pg_data_t *pgdat;
	unsigned long total = 0, reserved = 0, highmem = 0;

	printk("Mem-Info:\n");
	show_free_areas(filter);

	if (filter & SHOW_MEM_FILTER_PAGE_COUNT)
		return;

	for_each_online_pgdat(pgdat) {
		unsigned long flags;
		int zoneid;

		pgdat_resize_lock(pgdat, &flags);
		for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
			struct zone *zone = &pgdat->node_zones[zoneid];
			if (!populated_zone(zone))
				continue;

			total += zone->present_pages;
			reserved = zone->present_pages - zone->managed_pages;

			if (is_highmem_idx(zoneid))
				highmem += zone->present_pages;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}

	printk("%lu pages RAM\n", total);
	printk("%lu pages HighMem/MovableOnly\n", highmem);
	printk("%lu pages reserved\n", reserved);
#ifdef CONFIG_QUICKLIST
	printk("%lu pages in pagetable cache\n",
		quicklist_total_size());
#endif
}

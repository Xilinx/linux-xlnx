/*
 * arch/xtensa/mm/init.c
 *
 * Derived from MIPS, PPC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 * Copyright (C) 2014 - 2016 Cadence Design Systems Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Marc Gauthier
 * Kevin Chea
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>

#include <asm/bootparam.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/sysmem.h>

/*
 * Initialize the bootmem system and give it all low memory we have available.
 */

void __init bootmem_init(void)
{
	/* Reserve all memory below PHYS_OFFSET, as memory
	 * accounting doesn't work for pages below that address.
	 *
	 * If PHYS_OFFSET is zero reserve page at address 0:
	 * successfull allocations should never return NULL.
	 */
	if (PHYS_OFFSET)
		memblock_reserve(0, PHYS_OFFSET);
	else
		memblock_reserve(0, 1);

	early_init_fdt_scan_reserved_mem();

	if (!memblock_phys_mem_size())
		panic("No memory found!\n");

	min_low_pfn = PFN_UP(memblock_start_of_DRAM());
	min_low_pfn = max(min_low_pfn, PFN_UP(PHYS_OFFSET));
	max_pfn = PFN_DOWN(memblock_end_of_DRAM());
	max_low_pfn = min(max_pfn, MAX_LOW_PFN);

	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

	memblock_dump_all();
}


void __init zones_init(void)
{
	/* All pages are DMA-able, so we put them all in the DMA zone. */
	unsigned long zones_size[MAX_NR_ZONES] = {
		[ZONE_DMA] = max_low_pfn - ARCH_PFN_OFFSET,
#ifdef CONFIG_HIGHMEM
		[ZONE_HIGHMEM] = max_pfn - max_low_pfn,
#endif
	};
	free_area_init_node(0, zones_size, ARCH_PFN_OFFSET, NULL);
}

/*
 * Initialize memory pages.
 */

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	reset_all_zones_managed_pages();
	for (tmp = max_low_pfn; tmp < max_pfn; tmp++)
		free_highmem_page(pfn_to_page(tmp));
#endif

	max_mapnr = max_pfn - ARCH_PFN_OFFSET;
	high_memory = (void *)__va(max_low_pfn << PAGE_SHIFT);

	free_all_bootmem();

	mem_init_print_info(NULL);
	pr_info("virtual kernel memory layout:\n"
#ifdef CONFIG_HIGHMEM
		"    pkmap   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    fixmap  : 0x%08lx - 0x%08lx  (%5lu kB)\n"
#endif
#ifdef CONFIG_MMU
		"    vmalloc : 0x%08lx - 0x%08lx  (%5lu MB)\n"
#endif
		"    lowmem  : 0x%08lx - 0x%08lx  (%5lu MB)\n",
#ifdef CONFIG_HIGHMEM
		PKMAP_BASE, PKMAP_BASE + LAST_PKMAP * PAGE_SIZE,
		(LAST_PKMAP*PAGE_SIZE) >> 10,
		FIXADDR_START, FIXADDR_TOP,
		(FIXADDR_TOP - FIXADDR_START) >> 10,
#endif
#ifdef CONFIG_MMU
		VMALLOC_START, VMALLOC_END,
		(VMALLOC_END - VMALLOC_START) >> 20,
		PAGE_OFFSET, PAGE_OFFSET +
		(max_low_pfn - min_low_pfn) * PAGE_SIZE,
#else
		min_low_pfn * PAGE_SIZE, max_low_pfn * PAGE_SIZE,
#endif
		((max_low_pfn - min_low_pfn) * PAGE_SIZE) >> 20);
}

#ifdef CONFIG_BLK_DEV_INITRD
extern int initrd_is_mapped;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (initrd_is_mapped)
		free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

void free_initmem(void)
{
	free_initmem_default(-1);
}

static void __init parse_memmap_one(char *p)
{
	char *oldp;
	unsigned long start_at, mem_size;

	if (!p)
		return;

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return;

	switch (*p) {
	case '@':
		start_at = memparse(p + 1, &p);
		memblock_add(start_at, mem_size);
		break;

	case '$':
		start_at = memparse(p + 1, &p);
		memblock_reserve(start_at, mem_size);
		break;

	case 0:
		memblock_reserve(mem_size, -mem_size);
		break;

	default:
		pr_warn("Unrecognized memmap syntax: %s\n", p);
		break;
	}
}

static int __init parse_memmap_opt(char *str)
{
	while (str) {
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		parse_memmap_one(str);
		str = k;
	}

	return 0;
}
early_param("memmap", parse_memmap_opt);

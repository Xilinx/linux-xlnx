/*
 * arch/microblaze/mm/init.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/autoconf.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <asm/sections.h>
#include <asm/xparameters.h>

#ifdef CONFIG_BLUECAT_RFS
extern unsigned long bluecat_rfs_phys;
extern unsigned long bluecat_rfs_size;
void reserve_bluecat_rfs_mem(unsigned long start, unsigned long end);
#endif

char *klimit = _end;

void __init setup_memory(void)
{
	unsigned long m_start, m_end, map_size;

#ifdef CONFIG_BLUECAT_RFS
	m_start = PAGE_ALIGN((unsigned long)bluecat_rfs_phys + bluecat_rfs_size);
#else
	m_start = PAGE_ALIGN((unsigned long)klimit);
#endif
	m_end   = (CONFIG_XILINX_ERAM_START+CONFIG_XILINX_ERAM_SIZE-1);

	min_low_pfn = PFN_UP(CONFIG_XILINX_ERAM_START);
	max_mapnr = PFN_DOWN((CONFIG_XILINX_ERAM_START+CONFIG_XILINX_ERAM_SIZE-1));
	num_physpages = max_mapnr - min_low_pfn + 1;
	/* max_low_pfn is mis-named.  it holds number of pages, not
	 * the maximum page frame number in low memory */
	max_low_pfn = num_physpages;
	printk("%s: max_mapnr: %#lx\n", __FUNCTION__, max_mapnr);
	printk("%s: min_low_pfn: %#lx\n", __FUNCTION__, min_low_pfn);
	printk("%s: max_low_pfn: %#lx\n", __FUNCTION__, max_low_pfn);

	map_size = init_bootmem_node(NODE_DATA(0), PFN_UP(m_start), min_low_pfn, max_mapnr);

	free_bootmem(m_start+map_size, m_end - (m_start+map_size));
}

void __init paging_init(void)
{
	int i;
	unsigned long zones_size[MAX_NR_ZONES];

	/* we can DMA to/from any address.  put all page into
	 * ZONE_DMA. */
	zones_size[ZONE_DMA] = max_low_pfn;

	/* every other zones are empty */
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	free_area_init_node(0, NODE_DATA(0), zones_size,
			    NODE_DATA(0)->bdata->node_boot_start >> PAGE_SHIFT, NULL);
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)addr, 0xcc, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
	printk(KERN_INFO "Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

/* FIXME */
void show_mem(void)
{
}

void __init mem_init(void)
{
	high_memory = (void *)(CONFIG_XILINX_ERAM_START+CONFIG_XILINX_ERAM_SIZE-1);
	/* this will put all memory onto the freelists */
	totalram_pages += free_all_bootmem();

	printk(KERN_INFO "Memory: %luk/%luk available\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       num_physpages << (PAGE_SHIFT-10));
}

#ifdef CONFIG_BLUECAT_RFS
void free_bluecat_rfs_mem(unsigned long start, unsigned long end)
{
	printk ("Freeing BlueCat RFS memory: %ldk freed\n", (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif
